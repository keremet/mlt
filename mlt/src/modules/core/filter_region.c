/*
 * filter_region.c -- region filter
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

#include "transition_region.h"

#include <framework/mlt_filter.h>
#include <framework/mlt.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Filter processing.
*/

static mlt_frame filter_process( mlt_filter this, mlt_frame frame )
{
	// Get the properties of the filter
	mlt_properties properties = MLT_FILTER_PROPERTIES( this );

	// Get the region transition
	mlt_transition transition = mlt_properties_get_data( properties, "_transition", NULL );

	// Create the transition if not available
	if ( transition == NULL )
	{
		// Create the transition
		mlt_profile profile = mlt_service_profile( MLT_FILTER_SERVICE( this ) );
		transition = mlt_factory_transition( profile, "region", NULL );

		// Register with the filter
		mlt_properties_set_data( properties, "_transition", transition, 0, ( mlt_destructor )mlt_transition_close, NULL );

		// Pass a reference to this filter down
		mlt_properties_set_data( MLT_TRANSITION_PROPERTIES( transition ), "_region_filter", this, 0, NULL, NULL );
	}

	// Pass all properties down
	mlt_properties_pass( MLT_TRANSITION_PROPERTIES( transition ), properties, "" );

	// Process the frame
	return mlt_transition_process( transition, frame, NULL );
}

/** Constructor for the filter.
*/

mlt_filter filter_region_init( mlt_profile profile, mlt_service_type type, const char *id, char *arg )
{
	// Create a new filter
	mlt_filter this = mlt_filter_new( );

	// Further initialisation
	if ( this != NULL )
	{
		// Get the properties from the filter
		mlt_properties properties = MLT_FILTER_PROPERTIES( this );

		// Assign the filter process method
		this->process = filter_process;

		// Resource defines the shape of the region
		mlt_properties_set( properties, "resource", arg == NULL ? "rectangle" : arg );

		// Ensure that attached filters are handled privately
		mlt_properties_set_int( properties, "_filter_private", 1 );
	}

	// Return the filter
	return this;
}

