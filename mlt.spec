%define _unpackaged_files_terminate_build 1
%def_disable debug
%def_disable luma16bpp #if enabled produce 16bpp lumas instead of 8bpp

%ifarch %ix86 x86_64
%def_enable mmx
%def_enable sse
%else
%def_disable mmx
%def_disable sse
%endif


%define Name MLT
%define lname lib%name

Name: mlt
Version: 0.4.4
Release: alt1
Summary: Multimedia framework designed for television broadcasting
License: GPL
Group: Video
URL: http://sourceforge.net/projects/%name

Packager: Maxim Ivanov <redbaron@altlinux.org>

Source: %name-%version.tar
Patch1: %name-%version-%release.patch

BuildRequires: ImageMagick-tools gcc-c++ jackit-devel ladspa_sdk libSDL-devel
BuildRequires: libSDL_image-devel libX11-devel libavdevice-devel libavformat-devel
BuildRequires: libquicktime-devel libsamplerate-devel libsox-devel libswscale-devel
BuildRequires: libxml2-devel kde4libs-devel libqt4-devel

%description
%Name is a multimedia framework designed for television broadcasting.

%package utils
Summary: %name utils
Group: Video
License: GPL

%description utils
%Name utils.

%package -n %lname
Summary: %Name framework library
License: GPL
Group: System/Libraries

%description -n %lname
%Name is a multimedia framework designed for television broadcasting.

%package -n %lname-devel
Summary: Development files for %Name framework
License: GPL
Group: Development/C
Requires: %lname = %version-%release

%description -n %lname-devel
Development files for %Name framework.

%package -n %lname++
Summary: C++ wrapping for the MLT library
Group: System/Libraries

%description -n %lname++
This mlt sub-project provides a C++ wrapping for the MLT library.

%package -n %lname++-devel
Summary: Development files for %lname.
Group: Development/C++
Requires: %lname = %version-%release

%description -n %lname++-devel
Development files for %lname.

%prep
%setup -q
%patch1 -p1
#%patch2 -p1
#%patch3 -p1

%build
%ifarch x86_64
%add_optflags -DARCH_X86_64
%else
%ifarch %ix86
%add_optflags -DARCH_X86
%endif
%endif
export CFLAGS="%optflags"
%configure \
	--enable-gpl \
	--enable-motion-est \
	--avformat-swscale \
	%{subst_enable mmx} \
	%{subst_enable sse} \
	%{subst_enable debug} \
	--luma-compress \
        %if_disabled luma16bpp
	--luma-8bpp \
        %endif
	--kde-includedir=%_K4includedir \
        --kde-libdir=%_K4lib

%make_build

%install
%make DESTDIR=%buildroot install

%files -n %name-utils
#%doc docs/melt.txt
%_bindir/melt

%files -n %lname
#%doc docs/services.txt docs/westley.txt
%_libdir/%lname.so.*
%_libdir/%name
%_datadir/%name

%files -n %lname-devel
#%doc docs/framework.txt
%_includedir/%name
%_includedir/%name/framework
%_libdir/%lname.so
%_pkgconfigdir/%name-framework.pc

%files -n %lname++
%_libdir/%lname++.so.*

%files -n %lname++-devel
%_includedir/%name++
%_libdir/%lname++.so
%_pkgconfigdir/%name++.pc

%changelog
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
