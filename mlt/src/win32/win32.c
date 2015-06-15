/**
 * \file win32.c
 * \brief Miscellaneous utility functions for Windows.
 *
 * Copyright (C) 2003-2014 Meltytech, LLC
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

#include <errno.h> 
#include <time.h> 
#include <windows.h>
#include <pthread.h>

#include <iconv.h>
#include <locale.h>
#include <ctype.h>
#include "../framework/mlt_properties.h"

int usleep(unsigned int useconds)
{
	HANDLE timer;
	LARGE_INTEGER due;

	due.QuadPart = -(10 * (__int64)useconds);

	timer = CreateWaitableTimer(NULL, TRUE, NULL);
	SetWaitableTimer(timer, &due, 0, NULL, NULL, 0);
	WaitForSingleObject(timer, INFINITE);
	CloseHandle(timer);
	return 0;
}


int nanosleep( const struct timespec * rqtp, struct timespec * rmtp )
{
	if (rqtp->tv_nsec > 999999999) {
		/* The time interval specified 1,000,000 or more microseconds. */
		errno = EINVAL;
		return -1;
	}
	return usleep( rqtp->tv_sec * 1000000 + rqtp->tv_nsec / 1000 );
} 

int setenv(const char *name, const char *value, int overwrite)
{
	int result = 1; 
	if (overwrite == 0 && getenv (name))  {
		result = 0;
	} else  {
		result = SetEnvironmentVariable (name,value); 
	 }

	return result; 
} 

static int iconv_from_utf8( mlt_properties properties, const char *prop_name, const char *prop_name_out, const char* encoding )
{
	const char *text = mlt_properties_get( properties, prop_name );
	int result = -1;

	iconv_t cd = iconv_open( encoding, "UTF-8" );
	if ( text && ( cd != ( iconv_t )-1 ) ) {
		size_t inbuf_n = strlen( text );
		size_t outbuf_n = inbuf_n * 6;
		char *outbuf = mlt_pool_alloc( outbuf_n );
		char *outbuf_p = outbuf;

		memset( outbuf, 0, outbuf_n );

		if ( text != NULL && strcmp( text, "" ) && iconv( cd, &text, &inbuf_n, &outbuf_p, &outbuf_n ) != -1 )
			mlt_properties_set( properties, prop_name_out, outbuf );
		else
			mlt_properties_set( properties, prop_name_out, "" );

		mlt_pool_release( outbuf );
		result = 0;
	}
	iconv_close( cd );
	return result;
}

int mlt_properties_from_utf8( mlt_properties properties, const char *prop_name, const char *prop_name_out )
{
	int result = -1;
	// Get the locale name.
	const char *locale = setlocale( LC_CTYPE, NULL );
	if ( locale && strchr( locale, '.' ) ) {
		// Check for a code page in locale format = language_country.codepage.
		locale = strchr( locale, '.' ) + 1;
		if ( isdigit( locale[0] ) ) {
			// numeric code page
			char codepage[10];
			snprintf( codepage, sizeof(codepage), "CP%s", locale );
			result = iconv_from_utf8( properties, prop_name, prop_name_out, codepage );
		} else {
			// non-numeric code page possible on Windows?
			// TODO: some code pages may require conversion from numeric to iconv
			// compatible name. For example, maybe Shift-JIS or KOI8-R.
			result = iconv_from_utf8( properties, prop_name, prop_name_out, locale );
		}
	}
	if ( result < 0 ) {
		result = mlt_properties_set( properties, prop_name_out,
									 mlt_properties_get( properties, prop_name ) );
	}
	return result;
}
