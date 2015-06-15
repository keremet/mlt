/*
 * Copyright (C) 2014 Dan Dennedy <dan@dennedy.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "common.h"
#include <QApplication>
#include <QLocale>

#if defined(Q_OS_UNIX) && !defined(Q_OS_MAC)
#include <X11/Xlib.h>
#include <cstdlib>
#endif

bool createQApplicationIfNeeded(mlt_service service)
{
	if (!qApp) {
#if defined(Q_OS_UNIX) && !defined(Q_OS_MAC)
		XInitThreads();
		if (getenv("DISPLAY") == 0) {
			mlt_log_error(service,
				"The MLT Qt module requires a X11 environment.\n"
				"Please either run melt from an X session or use a fake X server like xvfb:\n"
				"xvfb-run -a melt (...)\n" );
			return false;
		}
#endif
		if (!mlt_properties_get(mlt_global_properties(), "qt_argv"))
			mlt_properties_set(mlt_global_properties(), "qt_argv", "MLT");
		static int argc = 1;
		static char* argv[] = { mlt_properties_get(mlt_global_properties(), "Qt argv") };
		new QApplication(argc, argv);
		const char *localename = mlt_properties_get_lcnumeric(MLT_SERVICE_PROPERTIES(service));
		QLocale::setDefault(QLocale(localename));
	}
	return true;
}
