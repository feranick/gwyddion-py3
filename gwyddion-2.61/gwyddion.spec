# $Id: gwyddion.spec.in 24771 2022-04-23 17:38:45Z yeti-dn $
Name:          gwyddion
Version:       2.61
Release:       1%{?dist}
Summary:       An SPM data visualization and analysis tool

Group:         Applications/Engineering
License:       GPLv2+
URL:           http://gwyddion.net/
Source0:       http://gwyddion.net/download/%{version}/%{name}-%{version}.tar.xz
BuildRoot:     %{_tmppath}/%{name}-%{version}-%{release}-root-%(id -un)
Requires(pre):    /sbin/ldconfig
Requires(postun): /sbin/ldconfig

BuildRequires: gcc
BuildRequires: gcc-c++
BuildRequires: pkgconfig(gtk+-2.0) >= 2.14
BuildRequires: pkgconfig(glib-2.0) >= 2.14
BuildRequires: pkgconfig(pango) >= 1.12
BuildRequires: pkgconfig(cairo) >= 1.6
BuildRequires: pkgconfig(fftw3) >= 3.1
BuildRequires: pkgconfig(gtkglext-1.0)
BuildRequires: pkgconfig(libxml-2.0)
BuildRequires: zlib-devel
BuildRequires: pkgconfig(libwebp)
BuildRequires: pkgconfig(OpenEXR)
BuildRequires: pkgconfig(cfitsio)
BuildRequires: pkgconfig(libpng)
BuildRequires: pkgconfig(unique-1.0)
BuildRequires: gettext
BuildRequires: desktop-file-utils >= 0.9
BuildRequires: findutils
BuildRequires: pkgconfig(xmu)
BuildRequires: pkgconfig(gtksourceview-2.0)
BuildRequires: pkgconfig(libzip)
BuildRequires: sed

%if 0%{?suse_version}
BuildRequires: pkg-config
%else
BuildRequires: pkgconfig
%endif

# __brp_python_bytecompile is for F27 and older
# __python redefinition for 28
# _python_bytecompile_extra for 29
# Since it is a mess which changes with every version, just try to disable
# byte compilation.
BuildRequires: perl >= 5.005
%if 0%{?fedora}
BuildRequires: perl-podlators
BuildRequires: rubypick
%global _python_bytecompile_extra 0
%global __python %{__python2}
%undefine __brp_python_bytecompile
%endif
%if 0%{?rhel}
BuildRequires: perl-podlators
BuildRequires: rubypick
%global __python %{__python2}
%undefine __brp_python_bytecompile
%endif

%if 0%{?fedora} >= 26
%define python2 python2
%else
%define python2 python
%endif
BuildRequires: %{python2}
BuildRequires: %{python2}-devel >= 2.7

%if 0%{?fedora}
BuildRequires: pygtk2-devel >= 2.10
BuildRequires: ruby(release) >= 1.8
%endif
%if 0%{?rhel}
BuildRequires: pygtk2-devel >= 2.10
BuildRequires: ruby(release) >= 1.8
%endif
%if 0%{?suse_version}
BuildRequires: python-gtk-devel >= 2.10
%endif
# otherwise...?

%define configureopts %{nil}

# The only packaged perl module is private, don't expose it.
%define __perl_provides %{nil}

%define pkglibdir %{_libdir}/%{name}
%define pkgdocdir %{_docdir}/%{name}
%define pkgdatadir %{_datadir}/%{name}
%define pkgincludedir %{_includedir}/%{name}
%if 0%{?suse_version}
%define docdirconfigurearg --docdir=%{pkgdocdir}
%else
%define docdirconfigurearg %{nil}
%endif
%define gtkdocdir %{_datadir}/gtk-doc/html


%package devel
Summary:       Headers, libraries and tools for Gwyddion module development
Group:         Development/Libraries
Requires:      %{name}%{?_isa} = %{version}
# This pulls everything else
Requires:      gtk2-devel%{?_isa} >= 2.8
Requires:      gtkglext-devel%{?_isa}
Requires:      perl(:MODULE_COMPAT_%(eval "`%{__perl} -V:version`"; echo $version))
Requires:      python(abi) = 2.7


%description
Gwyddion is a modular SPM (Scanning Probe Microsopy) data visualization and
analysis tool written with Gtk+.

It can be used for all most frequently used data processing operations
including: leveling, false color plotting, shading, filtering, denoising, data
editing, integral transforms, grain analysis, profile extraction, fractal
analysis, and many more.  The program is primarily focused on SPM data analysis
(e.g. data obtained from AFM, STM, NSOM, and similar microscopes).  However, it
can also be used for analysis of SEM (Scanning Electron Microscopy) data or any
other 2D data.


%description devel
Header files, libraries and tools for Gwyddion module and plug-in development.
This package also contains the API docmentation and sample plug-ins in various
programming languages.


%prep
%setup -q
# Don't install .la files.
sed -i -e '/# Install the pseudo-library/,/^$/d' ltmain.sh


%build
%configure %configureopts %docdirconfigurearg --disable-rpath --without-kde4-thumbnailer
make %{?_smp_mflags}


%install
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT
# Install the icon to the hicolor theme *and* to /usr/share/pixmaps because
# some distros expect it in one place, some in another.
mkdir -p $RPM_BUILD_ROOT%{_datadir}/pixmaps
install pixmaps/%{name}.png $RPM_BUILD_ROOT%{_datadir}/pixmaps
%find_lang %{name}

# Get rid of .la files if some silly distros (hello Mandriva) overwrote our
# fixed libtool with some crap.
find $RPM_BUILD_ROOT -name \*.la -print0 | xargs -0 rm -f

# Perl, Python, and Ruby modules are private, remove the Perl man page.
rm -f $RPM_BUILD_ROOT%{_mandir}/man3/Gwyddion::dump.*

# Byte-compile things that are meaningful to import.
# This requires F28+ and python3-devel?  Fuck it then.
##if 0##{?fedora}
##py_byte_compile ##{__python2} $RPM_BUILD_ROOT##{pkglibdir}/python/Gwyddion
##py_byte_compile ##{__python2} $RPM_BUILD_ROOT##{pkgdatadir}/pygwy
#endif

%clean
rm -rf $RPM_BUILD_ROOT


%post
/sbin/ldconfig
update-mime-database %{_datadir}/mime &>/dev/null || :
update-desktop-database &>/dev/null || :


%postun
/sbin/ldconfig
update-mime-database %{_datadir}/mime &>/dev/null || :
update-desktop-database &>/dev/null || :


%files -f %{name}.lang
%defattr(755,root,root)
%{_bindir}/%{name}
%{_bindir}/%{name}-thumbnailer
%defattr(-,root,root)
%doc AUTHORS COPYING NEWS README THANKS
%{pkgdatadir}/pixmaps/*.png
%{pkgdatadir}/pixmaps/*.ico
%{pkgdatadir}/gradients/*
%{pkgdatadir}/glmaterials/*
%{pkgdatadir}/pygwy/*
%{pkgdatadir}/ui/*
%{pkgdatadir}/user-guide-modules
%dir %{pkgdatadir}/pixmaps
%dir %{pkgdatadir}/gradients
%dir %{pkgdatadir}/glmaterials
%dir %{pkgdatadir}/pygwy
%dir %{pkgdatadir}/ui
%dir %{pkgdatadir}
%{_mandir}/man1/%{name}.1*
%{_mandir}/man1/%{name}-thumbnailer.1*
%{_datadir}/icons/hicolor/48x48/apps/%{name}.png
%{_datadir}/pixmaps/%{name}.png
%{_datadir}/metainfo/net.gwyddion.Gwyddion.appdata.xml
%{pkglibdir}/modules/cmap/*.so
%{pkglibdir}/modules/file/*.so
%{pkglibdir}/modules/graph/*.so
%{pkglibdir}/modules/layer/*.so
%{pkglibdir}/modules/process/*.so
%{pkglibdir}/modules/tool/*.so
%{pkglibdir}/modules/volume/*.so
%{pkglibdir}/modules/xyz/*.so
%{pkglibdir}/modules/*.so
%dir %{pkglibdir}/modules/cmap
%dir %{pkglibdir}/modules/file
%dir %{pkglibdir}/modules/graph
%dir %{pkglibdir}/modules/layer
%dir %{pkglibdir}/modules/process
%dir %{pkglibdir}/modules/tool
%dir %{pkglibdir}/modules/volume
%dir %{pkglibdir}/modules/xyz
%dir %{pkglibdir}/modules
%dir %{pkglibdir}
%{_libdir}/*.so.*
%{_datadir}/applications/%{name}.desktop
%{_datadir}/mime/packages/%{name}.xml
%{_datadir}/thumbnailers/%{name}.thumbnailer
%{_datadir}/gtksourceview-2.0/language-specs/pygwy.lang

%files devel
%defattr(-,root,root)
%doc devel-docs/CODING-STANDARDS
%doc data/%{name}.vim
%{pkgincludedir}/app/*.h
%{pkgincludedir}/libdraw/*.h
%{pkgincludedir}/libprocess/*.h
%{pkgincludedir}/libgwyddion/*.h
%{pkgincludedir}/libgwydgets/*.h
%{pkgincludedir}/libgwymodule/*.h
%dir %{pkgincludedir}/app
%dir %{pkgincludedir}/libdraw
%dir %{pkgincludedir}/libprocess
%dir %{pkgincludedir}/libgwyddion
%dir %{pkgincludedir}/libgwydgets
%dir %{pkgincludedir}/libgwymodule
%dir %{pkgincludedir}
%{_libdir}/*.so
%{python_sitearch}/gwy.so
%{_libdir}/pkgconfig/gwyddion.pc
%dir %{_libdir}/pkgconfig
# Documentation
%doc %{gtkdocdir}/libgwyapp/*
%doc %{gtkdocdir}/libgwydraw/*
%doc %{gtkdocdir}/libgwyprocess/*
%doc %{gtkdocdir}/libgwyddion/*
%doc %{gtkdocdir}/libgwydgets/*
%doc %{gtkdocdir}/libgwymodule/*
%doc %dir %{gtkdocdir}/libgwyapp
%doc %dir %{gtkdocdir}/libgwydraw
%doc %dir %{gtkdocdir}/libgwyprocess
%doc %dir %{gtkdocdir}/libgwyddion
%doc %dir %{gtkdocdir}/libgwydgets
%doc %dir %{gtkdocdir}/libgwymodule
%doc %dir %{gtkdocdir}
%doc %dir %{_datadir}/gtk-doc
%{pkglibdir}/include/gwyconfig.h
%dir %{pkglibdir}/include
# Plug-ins and plug-in devel stuff
%{pkglibdir}/perl/Gwyddion/*
%dir %{pkglibdir}/perl/Gwyddion
%dir %{pkglibdir}/perl
%{pkglibdir}/python/Gwyddion/*
%dir %{pkglibdir}/python/Gwyddion
%dir %{pkglibdir}/python
%{pkglibdir}/ruby/gwyddion/*
%dir %{pkglibdir}/ruby/gwyddion
%dir %{pkglibdir}/ruby
# Use filesystem permissions here.
%defattr(-,root,root,755)
%{pkgdocdir}/plugins/*
%dir %{pkgdocdir}/plugins
%dir %{pkgdocdir}

%changelog
* Tue Aug  8 2017 Yeti <yeti@gwyddion.net> - 2.61-1
- hello rpmlint, this package is partially generated
