# $Id: configure.ac 24806 2022-05-02 08:37:32Z yeti-dn $
dnl Process this file with autoconf to produce a configure script.
m4_define([gwy_version_major],[2])
m4_define([gwy_version_minor],[61])
m4_define([gwy_version_string],[gwy_version_major.gwy_version_minor])
AC_INIT([Gwyddion], [gwy_version_string], [klapetek@gwyddion.net])
# Use -release for unstable version libs instead of -version-info, because
# we *do* break compatibility each version
#LIBRARY_RELEASE=1.99.9
PACKAGE_URL="http://gwyddion.net/"
AC_PREREQ(2.62)
AC_CONFIG_SRCDIR(app/app.c)
AC_CONFIG_MACRO_DIR(m4)
AC_CONFIG_FILES(\
  Makefile \
  m4/Makefile \
  app/Makefile \
  libdraw/Makefile \
  libgwyddion/Makefile \
  libgwyddion/gwyversion.h \
  libgwydgets/Makefile \
  libgwymodule/Makefile \
  libprocess/Makefile \
  gwyddion/Makefile \
  modules/Makefile \
  modules/cmap/Makefile \
  modules/file/Makefile \
  modules/graph/Makefile \
  modules/layer/Makefile \
  modules/process/Makefile \
  modules/pygwy/Makefile \
  modules/tools/Makefile \
  modules/volume/Makefile \
  modules/xyz/Makefile \
  plugins/Makefile \
  pixmaps/Makefile \
  pixmaps/src/Makefile \
  po/Makefile.in \
  perl/Makefile \
  python/Makefile \
  ruby/Makefile \
  data/Makefile \
  data/glmaterials/Makefile \
  data/gradients/Makefile \
  utils/Makefile \
  utils/bundler \
  utils/mkosxlauncher \
  thumbnailer/Makefile \
  devel-docs/Makefile \
  devel-docs/libgwyapp/Makefile \
  devel-docs/libgwyapp/releaseinfo.xml \
  devel-docs/libgwyddion/Makefile \
  devel-docs/libgwyddion/releaseinfo.xml \
  devel-docs/libgwydgets/Makefile \
  devel-docs/libgwydgets/releaseinfo.xml \
  devel-docs/libgwydraw/Makefile \
  devel-docs/libgwydraw/releaseinfo.xml \
  devel-docs/libgwymodule/Makefile \
  devel-docs/libgwymodule/releaseinfo.xml \
  devel-docs/libgwyprocess/Makefile \
  devel-docs/libgwyprocess/releaseinfo.xml \
  gwyddion.spec:data/gwyddion.spec.in \
  data/gwyddion.pc \
  data/gwyddion.nsit \
  data/mingw-gwyddion-libs.spec
)
AC_CONFIG_HEADER(config.h)

AC_CANONICAL_HOST
AM_INIT_AUTOMAKE([1.11 gnu check-news silent-rules dist-xz])
AM_MAINTAINER_MODE
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

GWY_VERSION_MAJOR=gwy_version_major
GWY_VERSION_MINOR=gwy_version_minor
GWY_VERSION_STRING='"gwy_version_string"'
AC_SUBST(GWY_VERSION_MAJOR)
AC_SUBST(GWY_VERSION_MINOR)
AC_SUBST(GWY_VERSION_STRING)
AC_SUBST(LIBRARY_RELEASE)

#############################################################################
# Checks for programs and systems.
# Make libtool stop checking for FORTRAN.
m4_defun([_LT_AC_LANG_F77_CONFIG],[:])
AC_DISABLE_STATIC
AC_PROG_CC
AC_PROG_CXX
dnl FIXME: Can't do this, see math section below
dnl AC_PROG_CC_C99
AC_USE_SYSTEM_EXTENSIONS
AC_LIBTOOL_WIN32_DLL
AC_PROG_LIBTOOL
AC_PROG_INSTALL
AC_CHECK_PROGS([XSLTPROC],[xsltproc],[:])
AC_CHECK_PROGS([EPYDOC],[epydoc],[:])
GTK_DOC_CHECK(1.12)

#############################################################################
# 32bit/64bit
AC_CHECK_SIZEOF([void*])
if test $ac_cv_sizeof_voidp = 4; then
  arch_32bit=yes
else
  arch_32bit=no
fi

#############################################################################
# Win32.
AC_MSG_CHECKING([for native Microsoft Windows])
case "$host_os" in
  mingw*)
    os_mswin=yes
    HOST_CFLAGS="-mms-bitfields"
    HOST_LDFLAGS="-Wl,-subsystem,windows"
    case "$host_cpu" in
      *64) MSWIN_BITS=64;;
      *) MSWIN_BITS=32;;
    esac
    ;;
  *)
    os_mswin=no
    ;;
esac
AC_MSG_RESULT([$os_mswin])
AM_CONDITIONAL([OS_MSWIN],[test "$os_mswin" = "yes"])
AC_SUBST(MSWIN_BITS)

#############################################################################
# Darwin.
AC_MSG_CHECKING([for Darwin])
case "$host_os" in
  darwin*)
    os_darwin=yes
    ;;
  *)
    os_darwin=no
    ;;
esac
AC_MSG_RESULT([$os_darwin])
AM_CONDITIONAL([OS_DARWIN],[test "$os_darwin" = "yes"])

#############################################################################
# Home installation.
# If a user installs Gwyddion into a subdirectory of home, various XDG stuff
# will not end up where it is useful.  Try to put it there.
AC_ARG_ENABLE(home-installation,
              AC_HELP_STRING([--enable-home-installation],
                             [Install desktop integration files into your home]),
                             [],
                             [enable_home_installation=no])
AM_CONDITIONAL([HOME_INSTALLATION],[test "x$enable_home_installation" = xyes])
if test "x$enable_home_installation" = xyes; then
  xdg_target=home
else
  xdg_target=prefix
fi

# Do not set the actual XDG variable here!  It influences things.  Set *_DIR.
if test "x$XDG_DATA_HOME" = x; then
  XDG_DATA_HOME_DIR="$HOME/.local/share"
else
  XDG_DATA_HOME_DIR="$XDG_DATA_HOME"
fi
AC_SUBST(XDG_DATA_HOME_DIR)

#############################################################################
# Module symbol resolution and bundling.
# We try not to link modules with libraries and let symbols resolve through
# app, because it means faster app startup and much faster compilation.  But
# on some systems that isn't possible.
AC_ARG_ENABLE(library-bloat,
              AC_HELP_STRING([--enable-library-bloat],
                             [Make linking very slow]),
                             [],
                             [enable_library_bloat=no])
if test "x$enable_library_bloat" != xyes \
   && test "x$allow_undefined_flag" = xunsupported; then
  enable_library_bloat=yes
fi
AM_CONDITIONAL([MODULE_DEPENDENCIES],[test "x$enable_library_bloat" = xyes])

GWY_ENABLE([module-bundling],
           [module_bundling],
           [Bundling modules to one big shared library],
           [yes])
AM_CONDITIONAL([MODULE_BUNDLING],[test "x$enable_module_bundling" = xyes])

#############################################################################
# GLib tools
AC_CHECK_PROGS([GLIB_MKENUMS],[glib-mkenums])
AC_CHECK_PROGS([GLIB_GENMARSHAL],[glib-genmarshal])

#############################################################################
# XDG stuff
AC_CHECK_PROGS([UPDATE_DESKTOP_DATABASE],[update-desktop-database],:)
AC_CHECK_PROGS([UPDATE_MIME_DATABASE],[update-mime-database],:)
GWY_ENABLE([desktop-file-update],
           [desktop_file_update],
           [Update Freedesktop databases])
AM_CONDITIONAL([DESKTOP_FILE_UPDATE],[test "$enable_desktop_file_update" != no])
# This is always supported
THUMBNAILERS="manual"

#############################################################################
# Inkscape, for SVG -> PNG icon rendering
AC_CHECK_PROGS([INKSCAPE],[inkscape],:)
gwy_inkscape_export_new=no
if test "$INKSCAPE" != :; then
  AC_MSG_CHECKING([whether inkscape has 1.0-style command line options])
  inkscape_version=$($INKSCAPE --version 2>&1 | grep '^Inkscape' | sed -e 's/^Inkscape  *//' -e 's/ .*$//')
  GWY_VERSION_IFELSE($inkscape_version,[1.0.0],[gwy_inkscape_export_new=yes])
  AC_MSG_RESULT($gwy_inkscape_export_new)
fi
AM_CONDITIONAL([INKSCAPE_EXPORT_NEWSTYLE],[test "$gwy_inkscape_export_new" != no])

AC_CHECK_PROGS([PNGCRUSH],[pngcrush],:)
if test "$PNGCRUSH" != :; then
  AC_MSG_CHECKING([whether pngcrush has 1.8.9-style command line options])
  pngcrush_version=$($PNGCRUSH -version 2>&1 | grep '^ *pngcrush' | sed -e 's/,.*//' -e 's/ *pngcrush *//')
  GWY_VERSION_IFELSE($pngcrush_version,[1.8.9],
                     [PNGCRUSH_SILENCE=-warn; gwy_pngcrush_silent_new=yes],
                      [PNGCRUSH_SILENCE=-q; gwy_pngcrush_silent_new=no])
  AC_MSG_RESULT($gwy_pngcrush_silent_new)
  AC_SUBST(PNGCRUSH_SILENCE)
fi

#############################################################################
# Python
# Must override the default interpreter list because otherwise it simply tries
# to use the highest version, which can be python 3.x.
GWY_WITH([python],,[Install Python modules and plug-ins])
m4_define_default([_AM_PYTHON_INTERPRETER_LIST],[python3 python3.6 python3.7 python3.8 python3.9 python3.10 python2 python2.7 python2.6 python2.5 python2.4 python])
AM_PATH_PYTHON(3.10,,:)
AM_CONDITIONAL([HAVE_PYTHON],[test "$PYTHON" != : -a "$enable_python" != no])
AC_CHECK_SIZEOF([pid_t])

# Change backward slashes in paths to forwards slashes to avoid mixed-slash
# paths in MS Windows (forwards works just fine in MinGW).
pyexecdir=$(echo "$pyexecdir" | sed 's:\\:/:g')
am_cv_python_pyexecdir=$(echo "$am_cv_python_pyexecdir" | sed 's:\\:/:g')
pythondir=$(echo "$pythondir" | sed 's:\\:/:g')
am_cv_python_pythondir=$(echo "$am_cv_python_pythondir" | sed 's:\\:/:g')

AC_ARG_ENABLE(pygwy, AC_HELP_STRING([--enable-pygwy],
                                    [Build Python wrapper to Gwyddion]),[],
                                    [enable_pygwy=yes])

embed_pygtk=no
if test "x$enable_pygwy" != xno; then
  if test "$PYTHON" = : -o "x$enable_python" = no; then
    enable_pygwy=no
    pygwy_warn=" (needs python)"
  fi
fi

if test "x$os_mswin" != xyes; then
  # This is hopefully enough on Unix.
  if test "x$enable_pygwy" != xno; then
    GWY_PYTHON_DEVEL([3.10 3.9 3.8 3.7 3.6 2.7 2.6 2.5 2.4],,
                     [enable_pygwy=yes
                      pygwy_warn=" (no usable libpython)"])
  fi
else
  # There is no python-config in MS Windows.  Rely on the user to provide
  # PYTHON_INCLUDES and PYTHON_LDFLAGS.  Normally "-I/some/dir/include" and
  # "-L/some/dir/lib -lpython27" suffice.
  if test "x$enable_pygwy" != xno; then
    if test "x$PYTHON_INCLUDES" = x; then
      AC_MSG_NOTICE([please set PYTHON_INCLUDES; it is not going to work otherwise])
    fi
    if test "x$PYTHON_LDFLAGS" = x; then
      AC_MSG_NOTICE([please set PYTHON_LDFLAGS; it is not going to work otherwise])
    fi
    GWY_PYTHON_TRY_LINK([],
                        [enable_pygwy=no
                         pygwy_warn=" (cannot link with libpython)"])
  fi
fi

if test "x$enable_pygwy" != xno; then
  PKG_CHECK_MODULES(PYGTK,[pygtk-2.0 >= 2.10.0],[],
                    [embed_pygtk=yes; pygwy_warn=" (embedded pygtk2)"])
  if test "x$embed_pygtk" = xno; then
    if test "x$PYGTK_CODEGENDIR" = 'x'; then
      PYGTK_CODEGENDIR=`$PKG_CONFIG --variable=codegendir pygtk-2.0`
    fi
  else
    # Some silly OSes want to remove pygtk2.  We can build pygwy without
    # requiring the pygtk/pygobject codegen stuff to be installed.  We still
    # require the runtime packages.  These will be more difficult to embed.

    # The local sub directory with codegen.
    PYGTK_CODEGENDIR=pygtk-embed/codegen
    # The pygtk flags are reall just the GTK+ flags, except the -I for
    # pygobject.h.  But we know where we have it when embedding.
    PYGTK_CFLAGS="$GTK_CFLAGS -Ipygtk-embed/include/pygtk-2.0"
    PYGTK_LIBS="$GTK_LIBS"
  fi
  AC_SUBST(PYGTK_CFLAGS)
  AC_SUBST(PYGTK_LIBS)
  AC_SUBST(PYGTK_CODEGENDIR)
fi
AM_CONDITIONAL([ENABLE_PYGWY], [test "x$enable_pygwy" != xno])

#############################################################################
# Check for Perl
GWY_WITH([perl],,[Install Perl modules and plug-ins])
AC_PATH_PROGS(PERL,perl5 perl,:)
if test "$PERL" != :; then
  AC_PATH_PROGS(POD2MAN,pod2man,:)
fi
AM_CONDITIONAL([HAVE_PERL], [test "$PERL" != : -a "$enable_perl" != no])

#############################################################################
# Check for Ruby
# The outer [] are necessary to quote the inner [] inside Ruby
GWY_WITH([ruby],,[Install Ruby modules and plug-ins])
AC_PATH_PROGS(RUBY,ruby,:)
if test "$RUBY" != :; then
  AC_MSG_CHECKING([if ruby version >= 1.8])
  rbversion=`$RUBY --version | awk '{print $2}'`
  AS_VERSION_COMPARE($rbversion, [1.8],
    [ruby_version_ok=no; RUBY=:],
    [ruby_version_ok=yes],
    [ruby_version_ok=yes])
  AC_MSG_RESULT([$ruby_version_ok])
fi
AM_CONDITIONAL([HAVE_RUBY], [test "$RUBY" != : -a "$enable_ruby" != no])

#############################################################################
# With GCC, be paranoiac.  It should compile really cleanly.
if test "$GCC" = yes; then
  GWY_PROG_CC_OPTION([WNO_EMPTY_FMT],[-Wno-format-zero-length],,)
  GWY_PROG_CC_OPTION([WNO_SYS_HEADERS],[-Wno-system-headers],,)
  GWY_PROG_CC_OPTION([WNO_POINTER_SIGN],[-Wno-pointer-sign],,)
  GWY_PROG_CC_OPTION([WNO_UNUSED_PAR],[-Wno-unused-parameter],,)
  GWY_PROG_CC_OPTION([ERR_IFD],[-Werror=implicit-function-declaration],,)
  GWY_PROG_CC_OPTION([ERR_RET],[-Werror=return-type],,)
  # FIXME: We would like an error, but gcc can't make one w/o -std, which is
  # too much.
  GWY_PROG_CC_OPTION([WMIXED_DECL],[-Wdeclaration-after-statement],,)
  GWY_PROG_CC_OPTION([WREDUNDANT_DECLS],[-Wredundant-decls],,)
  GWY_PROG_CC_OPTION([WNO_MISSING_FIELD_INITIALIZERS],[-Wno-missing-field-initializers],,)
  # Fallthrough is usually *the* reason for choosing case statements over ifs.
  GWY_PROG_CC_OPTION([WNO_IMPLICIT_FALLTHROUGH],[-Wno-implicit-fallthrough],,)
  # We always has lots of typecasts to functions with fewer arguments.  And
  # codegen produces about a bazillion of them.  So, even though it may
  # undefined according to the standard (depends on cdecl), ignore the warning.
  GWY_PROG_CC_OPTION([WNO_CAST_FUNCTION_TYPE],[-Wno-cast-function-type],,)
  # The potential truncation gcc warns about is exactly the wanted result when
  # dealing with fixed-size fields in binary data formats.
  GWY_PROG_CC_OPTION([WNO_STRINGOP_TRUNCATION],[-Wno-stringop-truncation],,)
  # We want to do arithmetic and other type-unclean operations on enums.
  GWY_PROG_CC_OPTION([WNO_ENUM_CONVERSION],[-Wno-enum-conversion],,)
  # For OpenEXR until they somehow get rid of dynamic exception specification.
  GWY_PROG_CC_OPTION([WNO_DEPRECATED],[-Wno-deprecated],,)
  # Warns about things like warning(""); which prints a warning with no body,
  # something we occasionally do.  Must add -Wformat to make it work.  It is
  # implied by -Wall but we do not pass it here.
  GWY_PROG_CC_OPTION([WNO_FORMAT_ZERO_LENGTH],[-Wformat -Wno-format-zero-length],[PROG_CC_WNO_FORMAT_ZERO_LENGTH='-Wno-format-zero-length'],)
  # We do not rely on infs and nans working and they are stripped from inputs.
  GWY_PROG_CC_OPTION([FFAST_MATH],[-ffast-math],,)
  # This enables vectorisation.
  GWY_PROG_CC_OPTION([O3],[-O3],,)
  # This we need to Python code because macros from Python.h produce lots of
  # warnings.
  GWY_PROG_CC_OPTION([FNO_STRICT_ALIASING],[-fno-strict-aliasing],,)
  # Flags we can't use:
  # -Wstrict-protoypes makes gtkitemfactory.h noisy, but they have a
  #    comment there they can't fix it due to compatibility
  # -Wwrite-strings because it makes all struct initializations very noisy
  WARNING_CFLAGS="-Wall -W -Wshadow -Wpointer-arith -Wno-sign-compare -Wundef $PROG_CC_ERR_IFD $PROG_CC_ERR_RET $PROG_CC_WNO_SYS_HEADERS $PROG_CC_WNO_POINTER_SIGN $PROG_CC_WNO_EMPTY_FMT $PROG_CC_WMIXED_DECL $PROG_CC_WREDUNDANT_DECLS $PROG_CC_WNO_IMPLICIT_FALLTHROUGH $PROG_CC_WNO_CAST_FUNCTION_TYPE $PROG_CC_WNO_STRINGOP_TRUNCATION $PROG_CC_WNO_ENUM_CONVERSION $PROG_CC_WNO_FORMAT_ZERO_LENGTH"
  PYTHON_EXTRA_CFLAGS="$PROG_CC_WNO_UNUSED_PAR $PROG_CC_FNO_STRICT_ALIASING $PROG_CC_WNO_MISSING_FIELD_INITIALIZERS"
  GTKDOC_EXTRA_CFLAGS="$PROG_CC_WNO_UNUSED_PAR"
  HDRIMAGE_EXTRA_CFLAGS="$PROG_CC_WNO_DEPRECATED"
  PREMISE_CFLAGS="$PROG_CC_FFAST_MATH $PROG_CC_O3"
fi
AC_SUBST(PYTHON_EXTRA_CFLAGS)
AC_SUBST(GTKDOC_EXTRA_CFLAGS)
AC_SUBST(HDRIMAGE_EXTRA_CFLAGS)

#############################################################################
# Base libraries: X, Gtk+.
AC_PATH_XTRA
if test "x$x_includes" != x && test "x$x_includes" != xNONE; then
  CPPFLAGS="$CPPFLAGS -I$x_includes"
fi
if test "x$x_libraries" != x && test "x$x_libraries" != xNONE; then
  LDFLAGS="$LDFLAGS -L$x_libraries"
fi

# A dummy Gtk+ check, we perform another when we know whether to request
# pangoft2 or not.
PKG_CHECK_MODULES(GTK_BASE, [gtk+-2.0 >= 2.18.0])
PKG_CHECK_MODULES(GOBJECT, [gobject-2.0 >= 2.32.0 gthread-2.0])
PKG_CHECK_MODULES(GMODULE, [gmodule-2.0])
PKG_CHECK_MODULES(GIO, [gio-2.0])
GWY_CHECK_LIBM

#############################################################################
# Gtk+ targets (X11, Quartz)
AC_MSG_CHECKING([for Gdk target])
gdk_target=`$PKG_CONFIG --variable=target gtk+-2.0`
AC_MSG_RESULT([$gdk_target])
remote_backend=none
pangoft2=pangoft2
case "$gdk_target" in
  x11)
    have_xmu=no
    if test -z "$LIBXMU"; then
      XMU_ORIG_CFLAGS="$CFLAGS"
      XMU_ORIG_LDFLAGS="$LDFLAGS"
      CFLAGS="$CFLAGS $X_CFLAGS"
      LDFLAGS="$LDFLAGS $X_LIBS"
      AC_CHECK_LIB(Xmu, XmuClientWindow,
        [AC_CHECK_HEADER(X11/Xmu/WinUtil.h,
          [have_xmu=yes
           remote_backend=x11
           LIBXMU="$X_LIBS $X_PRE_LIBS -lX11 -lXmu -lXt"],
          )],
        [],
        [-lXt -lX11])
      CFLAGS="$XMU_ORIG_CFLAGS"
      LDFLAGS="$XMU_ORIG_LDFLAGS"
    else
      have_xmu=yes
      remote_backend=x11
    fi
    ;;
  quartz)
    PKG_CHECK_MODULES(GTK_MAC, [gtk-mac-integration],
      [AC_DEFINE(HAVE_GTK_MAC_INTEGRATION,1,[Define if we have the gtk-mac-integration library.])],
      [:])
    pangoft2=
    ;;
  win32)
    remote_backend=win32
    ;;
  *)
    ;;
esac
AC_SUBST(LIBXMU)

#############################################################################
# Full Gtk+ check
# XXX: We do not need pangoft2, but we export a deprecated function to obtain
# Pango FT2 context.  Get rid of this once possible.
PKG_CHECK_MODULES(GTK, [$pangoft2 cairo >= 1.2 pango >= 1.10 pangocairo gtk+-2.0 >= 2.8.0])

#############################################################################
# FFTW3
# Required.
PKG_CHECK_MODULES(FFTW3, [fftw3 >= 3.1])

#############################################################################
# OpenMP >= 3.1
# Optional.
GWY_OPENMP([201107],[3.1])

#############################################################################
# FFTW + OpenMP
# Optional.
# Pkg-config cannot tell us if FFTW libs are available with OpenMP support.
# We have to try linking with them.
fftw_openmp_libs=`echo "$FFTW3_LIBS " | sed 's/-lfftw3 /-lfftw3_omp -lfftw3 /'`
if test x$enable_openmp = xyes; then
AC_CACHE_CHECK([for FFTW3 library with OpenMP support],
  [ac_cv_gwy_fftw_openmp],
  [ye_PROG_CC_OPTION_cflags="$CFLAGS"
   ye_PROG_CC_OPTION_ldflags="$LDFLAGS"
  CFLAGS="$CFLAGS $FFTW3_CFLAGS $OPENMP_CFLAGS"
  LDFLAGS="$LDFLAGS $fftw_openmp_libs $OPENMP_CFLAGS"
  AC_LINK_IFELSE([AC_LANG_PROGRAM([[fftw_init_threads(); fftw_cleanup_threads();]])],
     [ac_cv_gwy_fftw_openmp=yes],
     [ac_cv_gwy_fftw_openmp=no])
  CFLAGS="$ye_PROG_CC_OPTION_cflags"
  LDFLAGS="$ye_PROG_CC_OPTION_ldflags"
])
fi

if test x$ac_cv_gwy_fftw_openmp = xyes; then
  AC_DEFINE(HAVE_FFTW_WITH_OPENMP,1,[Define if we have FFTW3 with OpenMP support.])
  FFTW3_LIBS="$fftw_openmp_libs"
  pkg_cv_FFTW3_LIBS="$fftw_openmp_libs"
fi

#############################################################################
# GtkSourceView
# Optional.
if test "x$enable_pygwy" != xno; then
  GWY_WITH([gtksourceview],,[Better Pygwy console user interface])
  if test "x$enable_gtksourceview" != "xno"; then
    PKG_CHECK_MODULES(GTKSOURCEVIEW,[gtksourceview-2.0],
                      [AC_DEFINE(HAVE_GTKSOURCEVIEW,1,
                                 [Define if we have the GtkSourceView package.])],
                      [enable_gtksourceview=no])
  fi
  if test "x$enable_gtksourceview" != "xno"; then
    GTKSOURCEVIEW_DEPENDENCY=gtksourceview-2.0
  else
    GTKSOURCEVIEW_DEPENDENCY=
    GTKSOURCEVIEW_CFLAGS="$GTK_CFLAGS"
    GTKSOURCEVIEW_LIBS="$GTK_LIBS"
  fi
  AC_SUBST([GTKSOURCEVIEW_DEPENDENCY])
fi

#############################################################################
# GtkGLExt
# Optional.
GWY_WITH([gl],,[OpenGL 3D view widgets])
if test "x$enable_gl" != "xno"; then
  # Repeat pangoft2 here to maintain assertion GTK flags are subset of GTKGLEXT
  PKG_CHECK_MODULES(GTKGLEXT,[$pangoft2 gtkglext-1.0],
                    [AC_DEFINE(HAVE_GTKGLEXT,1,
                               [Define if we have the GtkGLExt package.])],
                    [enable_gl=no; GL_WARN=" (needs GtkGLExt)"])
fi
if test "x$enable_gl" != "xno"; then
  GTKGLEXT_DEPENDENCY=gtkglext-1.0
else
  GTKGLEXT_DEPENDENCY=
  GTKGLEXT_CFLAGS="$GTK_CFLAGS"
  GTKGLEXT_LIBS="$GTK_LIBS"
fi
AC_SUBST([GTKGLEXT_DEPENDENCY])

#############################################################################
# libz
# Optional.  Used sometimes for data compression, e.g. in Createc, NRRD and
# MATLAB.
GWY_WITH([zlib],,[build with zlib support])
if test "x$enable_zlib" != xno && test -z "$ZLIB"; then
  AC_CHECK_LIB(z, inflate,
    [AC_CHECK_HEADER(zlib.h, ZLIB='-lz', [enable_zlib=no])],
    [enable_zlib=no],
    [])
fi
if test "x$enable_zlib" != xno && test -n "$ZLIB"; then
  AC_DEFINE(HAVE_ZLIB,1,[Define if we have the ZLIB library.])
fi
AM_CONDITIONAL([HAVE_ZLIB],[test "x$enable_zlib" != xno && test -n "$ZLIB"])
AC_SUBST(ZLIB)

#############################################################################
# libbz2
# Optional.  Used sometimes for data compression, e.g. in NRRD.
GWY_WITH([bzip2],,[build with bzip2 support])
if test "x$enable_bzip2" != xno && test -z "$BZIP2"; then
  AC_CHECK_LIB(bz2, BZ2_bzopen,
    [AC_CHECK_HEADER(bzlib.h, BZIP2='-lbz2', [enable_bzip2=no])],
    [enable_bzip2=no],
    [])
fi
if test "x$enable_bzip2" != xno && test -n "$BZIP2"; then
  AC_DEFINE(HAVE_BZIP2,1,[Define if we have the BZIP2 library.])
else
  BZIP2_WARN=" (needs bzip2)"
fi
AM_CONDITIONAL([HAVE_BZIP2],[test "x$enable_bzip2" != xno && test -n "$BZIP2"])
AC_SUBST(BZIP2)

#############################################################################
# ZIP support libraries.
# Optional.  Used to load the crazy zip-compressed-bunch-of-XML formats.
#
# The choices are:
#
# minizip 1.x
# It used to be preferred over libzip and is well tested, but is now obsolete.
#
# libzip
# Alternative to minizip.
#
# minizip 2.x or 3.x or whatever
# The new branch that broke everything and seems to be under heavy development.
# Not sure since which version it is actually usable.  They happily break API
# often; 3.x seems compatible but who knows what the future will bring.
enable_zip=auto
AC_ARG_WITH([zip],
  AS_HELP_STRING([--with-zip=ZIPLIB],
                 [ZIP support library (minizip1, minizip2, libzip or auto)]),
  [case "$withval" in
       minizip1|minizip2|libzip|auto|no) enable_zip="$withval";;
       yes) enable_zip=auto;;
       *) AC_MSG_ERROR([bad balue "$withval" for --with-zip]) ;;
   esac]
)
found_zip=no
enable_minizip1=no
enable_minizip2=no
enable_libzip=no
if test "x$found_zip" = xno && test "x$enable_zip" = xminizip1 -o "x$enable_zip" = xauto; then
  PKG_CHECK_MODULES(MINIZIP1, [minizip < 2.0],
                    [AC_DEFINE(HAVE_MINIZIP1,1,
                              [Define if we have the minizip 1.x package.])
                     found_zip=yes
                     enable_minizip1=yes],[:])
fi
if test "x$found_zip" = xno && test "x$enable_zip" = xlibzip -o "x$enable_zip" = xauto; then
  PKG_CHECK_MODULES(LIBZIP, [libzip >= 0.11],
                    [AC_DEFINE(HAVE_LIBZIP,1,
                              [Define if we have the libzip package.])
                     found_zip=yes
                     enable_libzip=yes],[:])
fi
if test "x$found_zip" = xno && test "x$enable_zip" = xminizip2 -o "x$enable_zip" = xauto; then
  PKG_CHECK_MODULES(MINIZIP2, [minizip >= 2.8],
                    [AC_DEFINE(HAVE_MINIZIP2,1,
                              [Define if we have the minizip 2.x (or newer) package.])
                     found_zip=yes
                     enable_minizip2=yes],[:])
  # Sometimes we get the minizip subdirectory in the include path, sometimes
  # we do not.  Check both.
  MINIZIP2_ORIG_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $MINIZIP2_CFLAGS"
  AC_CHECK_HEADERS([minizip/mz.h mz.h],[],[],[])
  CPPFLAGS="$MINIZIP2_ORIG_CPPFLAGS"
fi
AM_CONDITIONAL([HAVE_MINIZIP1],[test "x$enable_minizip1" != xno])
AM_CONDITIONAL([HAVE_MINIZIP2],[test "x$enable_minizip2" != xno])
AM_CONDITIONAL([HAVE_LIBZIP],[test "x$enable_libzip" != xno])
AM_CONDITIONAL([HAVE_ANY_ZIP],[test "x$found_zip" != xno])

#############################################################################
# PNG
# Optional.  Only directly used for 16bit greyscale PNG pixmap import/export.
enable_png=yes
PKG_CHECK_MODULES(PNG, [libpng],
                  [AC_DEFINE(HAVE_PNG,1,
                            [Define if we have the libpng package.])],
                  [enable_png=no])
AM_CONDITIONAL([HAVE_PNG],[test "x$enable_png" != xno])

#############################################################################
# WebP
# Optional.  Pixmap image format support.
enable_webp=yes
PKG_CHECK_MODULES(WEBP, [libwebp],
                  [AC_DEFINE(HAVE_WEBP,1,
                            [Define if we have the libwebp package.])],
                  [enable_webp=no])
AM_CONDITIONAL([HAVE_WEBP],[test "x$enable_webp" != xno])

#############################################################################
# C++
# Check if the compiler is "g++" but it is not GNU C++, which is a
# contradiction.  It is then just some fallback when no actual compiler is
# found.  We have to disable the hdrimage stuff completely.
AC_MSG_CHECKING([if the C++ compiler is a real thing])
if test "x$CXX" = "xg++" && test "x$GCC_CXX" != xyes; then
  have_cxx=no
else
  have_cxx=yes
fi
AC_MSG_RESULT([$have_cxx])
AM_CONDITIONAL([HAVE_CXX],[test "x$have_cxx" != xno])

#############################################################################
# OpenEXR 2.x+
# Optional.  Used for HDR greyscale OpenEXR pixmap import/export.
enable_exr=$have_cxx
PKG_CHECK_MODULES(EXR, [OpenEXR >= 2.0],
                  [AC_DEFINE(HAVE_EXR,1,
                            [Define if we have the OpenEXR 2.x+ package.])],
                  [enable_exr=no])
AM_CONDITIONAL([HAVE_EXR],[test "x$enable_exr" != xno])

#############################################################################
# HDF5
# Optional.  Used for Asylum Research Ergo import.
GWY_LIB_HDF5()
# We like calling all the variables `enable'.  Also, we want a Makefile
# conditional, not just a C preprocessor macro.
enable_hdf5=$with_hdf5
AM_CONDITIONAL([HAVE_HDF5],[test "x$enable_hdf5" != xno])

#############################################################################
# CFITSIO
# Optional.  Used for FITS image import.
PKG_CHECK_MODULES(CFITSIO, [cfitsio],
                  [AC_DEFINE(HAVE_CFITSIO,1,
                            [Define if we have the cfitsio package.])],
                  [enable_cfitsio=no])
AM_CONDITIONAL([HAVE_CFITSIO],[test "x$enable_cfitsio" != xno])

#############################################################################
# LibXML2.
# Optional, for SPML importer.
GWY_WITH([libxml2],,[build with libxml2 support])
if test "x$enable_libxml2" != "xno"; then
  PKG_CHECK_MODULES(XML2, [libxml-2.0 >= 2.6],
                    [AC_DEFINE(HAVE_XML2,1,
                               [Define if we have the libxml2 package.])],
                    [enable_libxml2=no])
fi
AM_CONDITIONAL([HAVE_XML2],[test "x$enable_libxml2" != xno])

#############################################################################
# JANSSON.
# Optional, for PS-PPT importer.
GWY_WITH([jansson],,[build with jansson support])
if test "x$enable_jansson" != "xno"; then
  PKG_CHECK_MODULES(JANSSON, [jansson >= 2.2],
                    [AC_DEFINE(HAVE_JANSSON,1,
                               [Define if we have the jansson package.])],
                    [enable_jansson=no])
fi
AM_CONDITIONAL([HAVE_JANSSON],[test "x$enable_jansson" != xno])

#############################################################################
# libunique
# Optional, a more modern remote control backend.
GWY_WITH([unique],,[libunique remote control backend])
if test "x$enable_unique" != "xno"; then
  PKG_CHECK_MODULES(UNIQUE,[unique-1.0],
                    [AC_DEFINE(HAVE_UNIQUE,1,
                               [Define if we have the unique package.])],
                    [enable_unique=no])
fi
if test "x$enable_unique" != xno; then
  remote_backend=unique
fi

#############################################################################
# Choose the remote control backend.
b=AS_TR_CPP([REMOTE_$remote_backend])
AC_DEFINE_UNQUOTED([REMOTE_BACKEND],$b,[Define to the remote control backend.])

#############################################################################
# KDE4 and Qt4 (for KDE thumbnailer plug-in)
# Very optional.
#
# Note we need headers of both, but actually link only with Qt.
#
# KDE4 uses cmake as the build system.  So either we are going to break things
# by mixing automake & cmake (because the thumbnailer needs to be linked with
# gwyddion libs), or we are going to break things by doing everything manually.
# In the latter case the poor user should have a better chance to fix it by
# setting CXXFLAGS, LIBS, etc. herself.
GWY_WITH([kde4-thumbnailer],
         [kde4_thumbnailer],
         [KDE4 thumbnail creator module],
         [no])

if test "x$enable_kde4_thumbnailer" != xno; then
  # Find kde4-config.
  AC_ARG_VAR([KDE4_CONFIG],[path to kde4-config utility])
  if test "x$ac_cv_env_KDE4_CONFIG_set" != xset; then
    AC_PATH_TOOL([KDE4_CONFIG],[kde4-config])
  fi
  if test "x$KDE4_CONFIG" = x; then
    enable_kde4_thumbnailer=no
  else
    # XXX: This leaves lots to desire.  What if someone installs Gwyddion to
    # home?  Let her at least overrride KDE4_MODULE_DIR
    KDE4_MODULE_DIR=`$KDE4_CONFIG --install module`
    KDE4_PREFIX=`$KDE4_CONFIG --prefix`
    KDE4_LIBSUFFIX=`$KDE4_CONFIG --libsuffix`
    QT4_PREFIX=`$KDE4_CONFIG --qt-prefix`
    QT4_LIBDIR=`$KDE4_CONFIG --qt-libraries`
  fi

  AC_LANG_PUSH([C++])
  KDE4_ORIG_LIBS="$LIBS"
  KDE4_ORIG_LDFLAGS="$LDFLAGS"
  KDE4_ORIG_CPPFLAGS="$CPPFLAGS"
  KDE4_CPPFLAGS="-I$KDE4_PREFIX/include/kde4 -I$QT4_PREFIX/include"
  KDE4_LDFLAGS="-L$KDE4_PREFIX/lib$KDE4_LIBSUFFIX -L$QT4_LIBDIR"
  KDE4_LIBS="-lQtCore -lQtGui"
  CPPFLAGS="$CPPFLAGS $KDE4_CPPFLAGS"
  LDFLAGS="$LDFLAGS $KDE4_LDFLAGS"
  LIBS="$LIBS $KDE4_LIBS"

  # Try to link with QtCore and QtGui.
  if test "x$enable_kde4_thumbnailer" != xno; then
    AC_MSG_CHECKING([for qVersion in $KDE4_LIBS])
    LIBS="$LIBS $KDE4_LIBS"
    AC_LINK_IFELSE([AC_LANG_SOURCE([char qVersion(void); int main(void){ return qVersion(); }])],
                   [:],[enable_kde4_thumbnailer=no])
    AC_MSG_RESULT([$enable_kde4_thumbnailer])
  fi

  # Try to compile with the corresponding headers.
  if test "x$enable_kde4_thumbnailer" != xno; then
    AC_CHECK_HEADER([QtCore/qstring.h],[:],[enable_kde4_thumbnailer=no])
  fi
  if test "x$enable_kde4_thumbnailer" != xno; then
    AC_CHECK_HEADER([QtGui/qimage.h],[:],[enable_kde4_thumbnailer=no])
  fi
  if test "x$enable_kde4_thumbnailer" != xno; then
    AC_CHECK_HEADER([kio/thumbcreator.h],[:],[enable_kde4_thumbnailer=no])
  fi

  CPPFLAGS="$KDE4_ORIG_CPPFLAGS"
  LDFLAGS="$KDE4_ORIG_LDFLAGS"
  LIBS="$KDE4_ORIG_LIBS"
  AC_LANG_POP([C++])
fi

AM_CONDITIONAL([KDE4_THUMBNAILER],[test "x$enable_kde4_thumbnailer" != xno])
if test "x$enable_kde4_thumbnailer" != xno; then
  THUMBNAILERS="$THUMBNAILERS KDE4"
fi

AC_SUBST(KDE4_MODULE_DIR)
AC_SUBST(KDE4_CPPFLAGS)
AC_SUBST(KDE4_LDFLAGS)
AC_SUBST(KDE4_LIBS)

#############################################################################
# Check for header files.
AC_MSG_CHECKING([for anyone actually reading this nonsense])
AC_MSG_RESULT([no])
AC_CHECK_HEADERS([stdbool.h])
AC_CHECK_HEADERS([GL/glext.h], [], [],
[[#include <GL/gl.h>]])

#############################################################################
# Check for typedefs, structures, and compiler characteristics.

#############################################################################
# Check for library functions.
GWY_CHECK_MATH_FUNCS([cbrt exp10 hypot pow10 acosh asinh atanh isinf isnan])
AC_CHECK_FUNCS([sincos log2 exp2 erf erfc lgamma tgamma j0 j1 y0 y1 memrchr memmem])

#############################################################################
# I18n
GETTEXT_PACKAGE=$PACKAGE_TARNAME
AM_GNU_GETTEXT([external])
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE",[Gettext package name])
AC_SUBST(GETTEXT_PACKAGE)

#############################################################################
# GLIBC >= 2.1
gl_GLIBC21
if test "x$GLIBC21" = xyes; then
  AC_DEFINE([HAVE_MEMMEM],1,[Define if we have working memmem()])
fi

#############################################################################
# Libs
BASIC_LIBS="$GOBJECT_LIBS $INTLLIBS $LIBM"
LIBS="$ORIG_LIBS"
AC_SUBST(BASIC_LIBS)
COMMON_LDFLAGS="$HOST_LDFLAGS"
AC_SUBST(COMMON_LDFLAGS)

COMMON_CFLAGS="$HOST_CFLAGS $WARNING_CFLAGS $GOBJECT_CFLAGS $GMODULE_CFLAGS $GTKGLEXT_CFLAGS $PREMISE_CFLAGS -DGLIB_DISABLE_DEPRECATION_WARNINGS"
AC_SUBST(COMMON_CFLAGS)
COMMON_CXXFLAGS="$HOST_CFLAGS $GOBJECT_CFLAGS $GMODULE_CFLAGS $GTKGLEXT_CFLAGS $PREMISE_CFLAGS -DGLIB_DISABLE_DEPRECATION_WARNINGS"
AC_SUBST(COMMON_CXXFLAGS)

#############################################################################
# Plugin-proxy
# Deprecated.
GWY_ENABLE([plugin-proxy],
           [plugin_proxy],
           [Build plug-in proxy module])
AM_CONDITIONAL([ENABLE_PLUGIN_PROXY],[test "$enable_plugin_proxy" != no])

#############################################################################
# RPM
AC_DEFINE_UNQUOTED(PACKAGE_URL,"$PACKAGE_URL",
                   [Define to package home page, as a string.])
AC_SUBST(PACKAGE_URL)

#############################################################################
# Generate gwyconfig
#
# FIXME: #define-ing symbols from libc is quite arrogant, even if they are
# missing on the system.  It can conflict with other packages.  Define
# something like gwy_foo() and use it instead of foo() in the source code,
# or, alternatively, make these definitions private.
#
AC_CONFIG_COMMANDS([gwyconfig.h],
[
  outfile=gwyconfig.h-tmp
  cat > $outfile <<\_______EOF
/*
 * gwyconfig.h
 *
 * This is a generated file.  Please modify 'configure.ac'.
 */

#ifndef __GWY_CONFIG_H__
#define __GWY_CONFIG_H__

_______EOF

  if test "x$enable_gl" = xno; then
    echo '#undef GWYDDION_HAS_OPENGL' >>$outfile
  else
    echo '#define GWYDDION_HAS_OPENGL 1' >>$outfile
  fi
  echo >>$outfile

  echo "#define GWY_SHARED_LIBRARY_EXTENSION \"$acl_shlibext\"" >>$outfile
  echo >>$outfile

  echo '/* Math functions become available when gwymathfallback.h is included. */' >>$outfile
  echo '/* In 2.x also when gwymath.h is included. */' >>$outfile

GWY_OUTPUT_MATH_FUNCS([cbrt exp10 hypot pow10 acosh asinh atanh isinf isnan], [$outfile])
  cat >>$outfile <<_______EOF

#endif /* __GWY_CONFIG_H__ */
_______EOF


  if cmp -s $outfile gwyconfig.h; then
    AC_MSG_NOTICE([gwyconfig.h is unchanged])
    rm -f $outfile
  else
    mv $outfile gwyconfig.h
  fi
],
[
  enable_gl=$enable_gl
  acl_shlibext=$acl_shlibext
  gwy_math_exp10=$gwy_math_exp10
  gwy_math_pow10=$gwy_math_pow10
  gwy_math_hypot=$gwy_math_hypot
  gwy_math_cbrt=$gwy_math_cbrt
  gwy_math_acosh=$gwy_math_acosh
  gwy_math_asinh=$gwy_math_asinh
  gwy_math_atanh=$gwy_math_atanh
  gwy_math_isinf=$gwy_math_isinf
  gwy_math_isnan=$gwy_math_isnan
])

#############################################################################
# Make msgmerge work on Darwin.  See
# http://subcommanderblog.wordpress.com/2009/05/16/msgmerge-on-macosx-and-no-such-file-or-directory/
if test "$os_darwin" = yes; then
  AC_MSG_NOTICE([fixing msgmerge to work on Darwin])
  AC_CONFIG_COMMANDS([osx-chmod],
  [
    chmod +a "" po/*.po
    chmod a+x utils/bundler utils/mkosxlauncher
  ])
fi

#############################################################################
# Create lists of included and exclued optional file formats.
# This table needs to be kept in sync with modules/file/Makefile.am.
cat >conftest.out <<EOF
found_zip? NanoObserver
found_zip? NanoScanTech
found_zip? OpenGPS
found_zip? PLUx
found_zip? SPMxFormat
found_zip? JPK force
found_zip? OlympusPOIR
found_zip? Keyence VK6
enable_libxml2? found_zip? APE DAX
enable_libxml2? enable_zlib? SPML
enable_libxml2? Anasys
enable_exr? have_cxx? OpenEXR
enable_cfitsio? FITS
enable_zlib? Createc/zlib
enable_zlib? NRRD/zlib
enable_zlib? RHK SM4 PRM meta/zlib
enable_bzip2? NRRD/bzip2
enable_png? have_cxx? PNG/16bit
have_cxx? BigTIFF
have_cxx? PGM/16bit
enable_webp? WebP
enable_hdf5? Ergo
enable_jansson? PS-PPT
EOF

for dep in enable_bzip2 enable_cfitsio enable_exr enable_jansson enable_libxml2 enable_png enable_webp enable_hdf5 enable_zlib found_zip have_cxx; do
  AS_VAR_COPY([x],[$dep])
  if test "x$x" != xno; then
    sed "s/$dep? //" conftest.out >conftest.tmp
  else
    sed "s/$dep? /#/" conftest.out >conftest.tmp
  fi
  cat conftest.tmp >conftest.out
done
rm -f conftest.tmp

enabled_formats=`grep -v '^#' conftest.out | sort | sed 's/$/, /' | tr -d '\n' | sed 's/, *$//'`
disabled_formats=`grep '^#' conftest.out | sort | sed 's/$/, /' | tr -d '#\n' | sed 's/, *$//'`
rm -f conftest.out

AC_OUTPUT

#############################################################################
# Print the summary.
pygwy_hl=
if test "x$enable_pygwy" = xyes; then
  if test "x$enable_gtksourceview" = xyes; then
    pygwy_hl=' (with syntax highlighting)'
  else
    pygwy_hl=' (no syntax highlighting)'
  fi
fi

echo "================================================================="
echo "Configuration Summary:"
echo "  OpenGL 3D widgets:                 $enable_gl$GL_WARN"
echo "  OpenMP parallelization:            $enable_openmp"
echo "  Remote control:                    $remote_backend"
echo "  Optional file formats included:    $enabled_formats"
echo "  Optional file formats excluded:    $disabled_formats"
echo "  Module linking:                    bundling=$enable_module_bundling, bloat=$enable_library_bloat"
echo "  Thumbnailers to build:             $THUMBNAILERS"
echo "  Python interface (pygwy):          $enable_pygwy$pygwy_hl$pygwy_warn"
echo "  Desktop integration files go to:   $xdg_target"
echo "================================================================="

#############################################################################
# Maintainer mode
if test "x$enable_maintainer_mode" = xyes; then
  missing_mmode_stuff=
  if test "x$PYTHON" = x || test "x$PYTHON" = x:; then
    missing_mmode_stuff="$missing_mmode_stuff, Python 2.x"
  fi
  if test "x$GLIB_MKENUMS" = x || test "x$GLIB_MKENUMS" = x:; then
    missing_mmode_stuff="$missing_mmode_stuff, glib-mkenums"
  fi
  if test "x$GLIB_GENMARSHAL" = x || test "x$GLIB_GENMARSHAL" = x:; then
    missing_mmode_stuff="$missing_mmode_stuff, glib-genmarshal"
  fi
  if test "x$INKSCAPE" = x || test "x$INKSCAPE" = x:; then
    missing_mmode_stuff="$missing_mmode_stuff, Inkscape"
  fi
  if test "x$XSLTPROC" = x || test "x$XSLTPROC" = x:; then
    missing_mmode_stuff="$missing_mmode_stuff, xsltproc"
  fi
  if test "x$enable_gtk_doc" != xyes; then
    missing_mmode_stuff="$missing_mmode_stuff, gtk-doc"
  fi
  if test "x$PNGCRUSH" = x || test "x$PNGCRUSH" = x:; then
    missing_mmode_stuff="$missing_mmode_stuff, pngcrush"
  fi
  if test "x$enable_pygwy" = xyes; then
    if test "x$EPYDOC" = x || test "x$EPYDOC" = x:; then
      missing_mmode_stuff="$missing_mmode_stuff, epydoc"
    fi
  fi
  missing_mmode_stuff=`echo "$missing_mmode_stuff" | sed 's/^, //'`
  if test "x$missing_mmode_stuff" != x; then
    AC_MSG_WARN([Missing maintainer mode tools.
*******************************************************************************
  Maintainer mode is enabled, but some tools used to generate various files
  are missing:

     $missing_mmode_stuff

  Depending on what you are doing and why you enabled the maintainer mode,
  this may be OK or things may break horribly.
*******************************************************************************])
  fi
fi

# vim: set ts=2 sw=2 et :
