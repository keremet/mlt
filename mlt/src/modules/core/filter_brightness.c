/*
 * filter_brightness.c -- gamma filter
 * Copyright (C) 2003-2004 Ushodaya Enterprises Limited
 * Author: Charles Yates <charles.yates@pandora.be>
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

#include <framework/mlt_filter.h>
#include <framework/mlt_frame.h>

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define CLAMP( x, min, max ) (x) < (min) ? (min) : (x) > (max) ? (max) : (x)

/** Do it :-).
*/

static int filter_get_image( mlt_frame this, uint8_t **image, mlt_image_format *format, int *width, int *height, int writable )
{
	// Get the image
	*format = mlt_image_yuv422;
	int error = mlt_frame_get_image( this, image, format, width, height, 1 );

	// Only process if we have no error and a valid colour space
	if ( error == 0 )
	{
		// Get the brightness level
		double level = mlt_properties_get_double( MLT_FRAME_PROPERTIES( this ), "brightness" );

		// Only process if level is something other than 1
		if ( level != 1.0 )
		{
			int i = *width * *height + 1;
			uint8_t *p = *image;
			int32_t m = level * ( 1 << 16 );
			int32_t n = 128 * ( ( 1 << 16 ) - m );

			while ( --i )
			{
				p[0] = CLAMP( (p[0] * m) >> 16, 16, 235 );
				p[1] = CLAMP( (p[1] * m + n) >> 16, 16, 240 );
				p += 2;
			}
		}
	}

	return error;
}

/** Filter processing.
*/

static mlt_frame filter_process( mlt_filter this, mlt_frame frame )
{
	// Get the starting brightness level
	double level = fabs( mlt_properties_get_double( MLT_FILTER_PROPERTIES( this ), "start" ) );

	// If there is an end adjust gain to the range
	if ( mlt_properties_get( MLT_FILTER_PROPERTIES( this ), "end" ) != NULL )
	{
		// Determine the time position of this frame in the transition duration
		double end = fabs( mlt_properties_get_double( MLT_FILTER_PROPERTIES( this ), "end" ) );
		level += ( end - level ) * mlt_filter_get_progress( this, frame );
	}

	// Push the frame filter
	mlt_properties_set_double( MLT_FRAME_PROPERTIES( frame ), "brightness", level );
	mlt_frame_push_get_image( frame, filter_get_image );

	return frame;
}

/** Constructor for the filter.
*/

mlt_filter filter_brightness_init( mlt_profile profile, mlt_service_type type, const char *id, char *arg )
{
	mlt_filter this = mlt_filter_new( );
	if ( this != NULL )
	{
		this->process = filter_process;
		mlt_properties_set( MLT_FILTER_PROPERTIES( this ), "start", arg == NULL ? "1" : arg );
	}
	return this;
}

