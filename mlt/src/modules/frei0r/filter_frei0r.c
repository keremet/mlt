/*
 * filter_frei0r.c -- frei0r filter
 * Copyright (c) 2008 Marco Gittler <g.marco@freenet.de>
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

#include <framework/mlt.h>

#include "frei0r_helper.h"
#include <string.h>
static int filter_get_image( mlt_frame this, uint8_t **image, mlt_image_format *format, int *width, int *height, int writable )
{

	mlt_filter filter = mlt_frame_pop_service( this );
	mlt_properties properties = MLT_FILTER_PROPERTIES( filter );
	*format = mlt_image_rgb24a;
	mlt_log_debug( MLT_FILTER_SERVICE( filter ), "frei0r %dx%d\n", *width, *height );
	int error = mlt_frame_get_image( this, image, format, width, height, 0 );

	if ( error == 0 && *image )
	{
		mlt_position length = mlt_filter_get_out( filter ) - mlt_filter_get_in( filter ) + 1;
		mlt_position time = mlt_properties_get_position( properties, "_filter_position" );
		double position = ( double )( time ) / ( double )( length );
		
		process_frei0r_item( filter_type, position, properties, this, image, width, height );
		
	}

	return error;
}


mlt_frame filter_process( mlt_filter this, mlt_frame frame )
{
	mlt_frame_push_service( frame, this );
	// Determine the time position of this frame
	mlt_properties_set_position( MLT_FILTER_PROPERTIES( this ), "_filter_position", mlt_frame_get_position( frame ) -  mlt_filter_get_in( this ) );
	
	mlt_frame_push_get_image( frame, filter_get_image );
	return frame;
}

void filter_close( mlt_filter this )
{
	destruct( MLT_FILTER_PROPERTIES ( this ) );
}
