%define set_disable() %{expand:%%force_disable %{1}} %{expand:%%undefine _enable_%{1}}
%define set_enable() %{expand:%%force_enable %{1}} %{expand:%%undefine _disable_%{1}}
%define mIF_ver_gt() %if "%(rpmvercmp '%1' '%2')" > "0"
%define mIF_ver_gteq() %if "%(rpmvercmp '%1' '%2')" >= "0"
%define mIF_ver_lt() %if "%(rpmvercmp '%2' '%1')" > "0"
%define mIF_ver_lteq() %if "%(rpmvercmp '%2' '%1')" >= "0"

%def_disable debug
%def_disable vdpau

%define Name MLT
%define mlt_sover 6
%define libmlt libmlt%mlt_sover
%define mltxx_sover 3
%define libmltxx libmlt++%mltxx_sover

Name: mlt
Version: 0.9.6
Release: alt1

Summary: Multimedia framework designed for television broadcasting
License: GPLv3
Group: Video
URL: http://sourceforge.net/projects/%name

Packager: Maxim Ivanov <redbaron@altlinux.org>

Source: %name-%version.tar
Source1: mlt++-config.h
Patch1: mlt-0.9.2-alt-configure-mmx.patch
Patch2: mlt-0.9.0-alt-no-version-script.patch
# SuSE
Patch10: libmlt-0.8.2-vdpau.patch

BuildRequires: ImageMagick-tools gcc-c++ jackit-devel ladspa_sdk libSDL-devel
BuildRequires: libSDL_image-devel libX11-devel libavdevice-devel libavformat-devel
BuildRequires: libquicktime-devel libsamplerate-devel libsox-devel libswscale-devel
BuildRequires: libxml2-devel kde4libs-devel libqt4-devel swig python-devel
BuildRequires: frei0r-devel libalsa-devel
%if_enabled vdpau
BuildRequires: libvdpau-devel
%endif

%description
%Name is a multimedia framework designed for television broadcasting.

%package utils
Summary: %name utils
Group: Video
License: GPL
%description utils
%Name utils.

%package -n %libmlt
Summary: %Name framework library
License: GPL
Group: System/Libraries
%mIF_ver_lt %version 0.10
Provides: libmlt = %EVR
Obsoletes: libmlt < %EVR
%endif
%description -n %libmlt
%Name is a multimedia framework designed for television broadcasting.

%package -n %libmltxx
Summary: C++ wrapping for the MLT library
Group: System/Libraries
%mIF_ver_lt %version 0.10
Provides: libmlt++ = %EVR
Obsoletes: libmlt++ < %EVR
%endif
%description -n %libmltxx
This mlt sub-project provides a C++ wrapping for the MLT library.

%package -n libmlt-devel
Summary: Development files for %Name framework
License: GPL
Group: Development/C
%description -n libmlt-devel
Development files for %Name framework.

%package -n libmlt++-devel
Summary: Development files for %Name.
Group: Development/C++
%description -n libmlt++-devel
Development files for %Name.

%package -n python-module-%name
Summary: Python package to work with %Name
Group: Development/Python
%description -n python-module-%name
This module allows to work with %Name using python..

%prep
%setup -q
%patch1 -p1
%patch2 -p1
%patch10 -p0

[ -f src/mlt++/config.h ] || \
    install -m 0644 %SOURCE1 src/mlt++/config.h

VDPAU_SONAME=`readelf -a %_libdir/libvdpau.so | grep SONAME| sed 's/.*\[//'| sed 's/\].*//'`
sed -i "s/__VDPAU_SONAME__/${VDPAU_SONAME}/" src/modules/avformat/vdpau.c

%build
export CC=gcc CXX=g++ CFLAGS="%optflags" QTDIR=%_qt4dir
%configure \
	--enable-gpl --enable-gpl3 \
	--target-os=Linux \
%ifarch x86_64
	--target-arch=%_target_cpu \
%endif
	%if_enabled vdpau
	--avformat-vdpau \
	%else
	--avformat-no-vdpau \
	%endif
	%ifnarch %ix86 x86_64
	--disable-mmx \
	--disable-sse \
	--disable-sse2 \
	%endif
	%ifarch i586
	--disable-mmx \
	%endif
	%{subst_enable debug} \
	--without-kde \
	--kde-includedir=%_K4includedir \
        --kde-libdir=%_K4lib \
        --swig-languages=python \
        #
#	--luma-compress \

%make_build

%install
%make DESTDIR=%buildroot install
install -d %buildroot%python_sitelibdir
install -pm 0644 src/swig/python/%name.py %buildroot%python_sitelibdir/
install -pm 0755 src/swig/python/_%name.so %buildroot%python_sitelibdir/

%files -n %name-utils
#%doc docs/melt.txt
%_bindir/melt

%files -n %libmlt
#%doc docs/services.txt docs/westley.txt
%_libdir/libmlt.so.%mlt_sover
%_libdir/libmlt.so.*
%_libdir/mlt
%_datadir/mlt

%files -n %libmltxx
%_libdir/libmlt++.so.%mltxx_sover
%_libdir/libmlt++.so.*

%files -n python-module-%name
%python_sitelibdir/*

%files -n libmlt-devel
#%doc docs/framework.txt
%_includedir/mlt/
%_libdir/libmlt.so
%_pkgconfigdir/mlt-framework.pc

%files -n libmlt++-devel
%_includedir/mlt++/
%_libdir/libmlt++.so
%_pkgconfigdir/mlt++.pc

%changelog
* Mon Jun 15 2015 Sergey V Turchin <zerg@altlinux.org> 0.9.6-alt1
- new version

* Thu Apr 16 2015 Sergey V Turchin <zerg@altlinux.org> 0.9.2-alt1.M70P.1
- build for M70P

* Fri Feb 27 2015 Denis Smirnov <mithraen@altlinux.ru> 0.9.2-alt2
- rebuild with new sox

* Fri Oct 17 2014 Sergey V Turchin <zerg@altlinux.org> 0.9.2-alt1
- new version

* Fri Oct 17 2014 Sergey V Turchin <zerg@altlinux.org> 0.9.0-alt0.M70P.1
- built for M70P

* Tue May 27 2014 Sergey V Turchin <zerg@altlinux.org> 0.9.0-alt1
- new version

* Tue Oct 08 2013 Sergey V Turchin <zerg@altlinux.org> 0.8.8-alt3
- rebuilt with new libav

* Thu May 23 2013 Sergey V Turchin <zerg@altlinux.org> 0.8.8-alt2
- disable VDPAU by default ( http://www.kdenlive.org/mantis/view.php?id=3070 )

* Thu Jan 31 2013 Sergey V Turchin <zerg@altlinux.org> 0.8.8-alt1
- new version

* Tue Oct 16 2012 Denis Smirnov <mithraen@altlinux.ru> 0.7.8-alt1.1
- rebuild with new sox

* Wed May 16 2012 Sergey V Turchin <zerg@altlinux.org> 0.7.8-alt1
- new version

* Fri May 04 2012 Sergey V Turchin <zerg@altlinux.org> 0.7.6-alt0.M60P.2
- rebuild with libav

* Thu Dec 29 2011 Sergey V Turchin <zerg@altlinux.org> 0.7.6-alt0.M60P.1
- built for M60P

* Mon Dec 12 2011 Sergey V Turchin <zerg@altlinux.org> 0.7.6-alt1
- new version

* Sat Oct 22 2011 Vitaly Kuznetsov <vitty@altlinux.ru> 0.7.4-alt1.1
- Rebuild with Python-2.7

* Fri Aug 12 2011 Sergey V Turchin <zerg@altlinux.org> 0.7.4-alt0.M60P.1
- built for M60P

* Thu Aug 11 2011 Sergey V Turchin <zerg@altlinux.org> 0.7.4-alt1
- new version

* Fri Jul 08 2011 Sergey V Turchin <zerg@altlinux.org> 0.7.2-alt1
- new version

* Wed Apr 27 2011 Sergey V Turchin <zerg@altlinux.org> 0.7.0-alt1
- new version

* Thu Sep 16 2010 Sergey V Turchin <zerg@altlinux.org> 0.5.10-alt0.M51.1
- built for M51

* Wed Sep 15 2010 Sergey V Turchin <zerg@altlinux.org> 0.5.10-alt1
- new version

* Thu Jun 17 2010 Sergey V Turchin <zerg@altlinux.org> 0.5.4-alt1.M51.1
- built for M51

* Thu Jun 17 2010 Sergey V Turchin <zerg@altlinux.org> 0.5.4-alt2
- fix build flags

* Thu Jun 17 2010 Sergey V Turchin <zerg@altlinux.org> 0.5.4-alt1
- 0.5.4

* Wed Apr 14 2010 Maxim Ivanov <redbaron at altlinux.org> 0.5.2-alt1
- 0.5.2

* Tue Jan 12 2010 Slava Dubrovskiy <dubrsl@altlinux.org> 0.4.6-alt2
- Add subpackage python-module-%name

* Thu Nov 12 2009 Maxim Ivanov <redbaron at altlinux.org> 0.4.6-alt1
- 0.4.6
- mlt++.so.2 -> mlt++.so.3 soname change

* Fri Jul 31 2009 Denis Smirnov <mithraen@altlinux.ru> 0.4.4-alt1
- 0.4.4

* Sun Jun 28 2009 Maxim Ivanov <redbaron at altlinux.org> 0.4.2-alt1.git2b33565
- Bump to 0.4.2
- inigo utility renamed to melt
- mlt-config removed, use `pkg-config mlt-framework` instead
- miracle, humperdink and valerie are moved to separate project
- mlt++ is not part of main tree

* Sat May 16 2009 Maxim Ivanov <redbaron at altlinux.org> 0.3.8-alt2
- Fix missed libm dynamic link, closes #20024

* Sat Apr 18 2009 Maxim Ivaniv <redbaron at altlinux.org> 0.3.8-alt1
- 0.3.8

* Thu Feb 05 2009 Valery Inozemtsev <shrek@altlinux.ru> 0.3.6-alt1
- 0.3.6

* Wed Dec 31 2008 Valery Inozemtsev <shrek@altlinux.ru> 0.3.4-alt1
- 0.3.4

* Fri Dec 12 2008 Valery Inozemtsev <shrek@altlinux.ru> 0.3.2-alt1
- 0.3.2

* Thu Aug 07 2008 Valery Inozemtsev <shrek@altlinux.ru> 0.3.0-alt1
- 0.3.0

* Fri Jun 13 2008 Valery Inozemtsev <shrek@altlinux.ru> 0.2.5-alt1.svn1045
- rebuild with libquicktime.so.102

* Thu Dec 20 2007 Valery Inozemtsev <shrek@altlinux.ru> 0.2.5-alt0.svn1045
- SVN revision r1045 2007-12-08

* Mon Sep 03 2007 Alexey Morsov <swi@altlinux.ru> 0.2.4-alt1
- 0.2.4
- new patch set (thanks to shrek@)
- lot of bug fixes

* Fri Jun 29 2007 Alexey Morsov <swi@altlinux.ru> 0.2.2-alt0.7
- changes for new ffmpeg

* Mon Apr 23 2007 Alexey Morsov <swi@altlinux.ru> 0.2.2-alt0.6
- add patch from gentoo to fix mmx on ix86
- disable mmx

* Fri Apr 20 2007 Alexey Morsov <swi@altlinux.ru> 0.2.2-alt0.5
- remove disable_fox and arch dependencies from spec (for sox >= 13.0)
- add patch for build with sox
- add sox in BR due to bug in req's in sox-devel

* Wed Feb 14 2007 Alexey Morsov <swi@altlinux.ru> 0.2.2-alt0.4
- add Packager (swi@)

* Wed Feb 14 2007 Led <led@altlinux.ru> 0.2.2-alt0.3
- fixed %name-0.2.2-modulesdir.patch

* Tue Feb 13 2007 Led <led@altlinux.ru> 0.2.2-alt0.2
- added %name-0.2.2-configure.patch
- added %name-0.2.2-x86_64.patch

* Tue Feb 13 2007 Led <led@altlinux.ru> 0.2.2-alt0.1
- initial build
- added %name-0.2.2-modulesdir.patch
