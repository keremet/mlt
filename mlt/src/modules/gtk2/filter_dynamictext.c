/*
 * filter_dynamictext.c -- dynamic text overlay filter
 * Copyright (C) 2011 Ushodaya Enterprises Limited
 * Author: Brian Matherly <pez4brian@yahoo.com>
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
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/types.h> // for stat()
#include <sys/stat.h>  // for stat()
#include <unistd.h>    // for stat()
#include <time.h>      // for strftime() and gtime()

#define MAX_TEXT_LEN 512

/** Get the next token and indicate whether it is enclosed in "# #".
*/
static int get_next_token(char* str, int* pos, char* token, int* is_keyword)
{
	int token_pos = 0;
	int str_len = strlen( str );

	if( (*pos) >= str_len || str[*pos] == '\0' )
	{
		return 0;
	}

	if( str[*pos] == '#' )
	{
		*is_keyword = 1;
		(*pos)++;
	}
	else
	{
		*is_keyword = 0;
	}

	while( *pos < str_len && token_pos < MAX_TEXT_LEN - 1)
	{
		if( str[*pos] == '\\' && str[(*pos) + 1] == '#' )
		{
			// Escape Sequence - "#" preceeded by "\" - copy the # into the token.
			token[token_pos] = '#';
			token_pos++;
			(*pos)++; // skip "\"
			(*pos)++; // skip "#"
		}
		else if( str[*pos] == '#' )
		{
			if( *is_keyword )
			{
				// Found the end of the keyword
				(*pos)++;
			}
			break;
		}
		else
		{
			token[token_pos] = str[*pos];
			token_pos++;
			(*pos)++;
		}
	}

	token[token_pos] = '\0';

	return 1;
}

static void get_timecode_str( mlt_filter filter, mlt_frame frame, char* text )
{
	int frames = mlt_frame_get_position( frame );
	double fps = mlt_profile_fps( mlt_service_profile( MLT_FILTER_SERVICE( filter ) ) );
	char tc[12] = "";
	if (fps == 0)
	{
		strncat( text, "-", MAX_TEXT_LEN - strlen( text ) - 1 );
	}
	else
	{
		int seconds = frames / fps;
		frames = frames % lrint( fps );
		int minutes = seconds / 60;
		seconds = seconds % 60;
		int hours = minutes / 60;
		minutes = minutes % 60;
		sprintf(tc, "%.2d:%.2d:%.2d:%.2d", hours, minutes, seconds, frames);
		strncat( text, tc, MAX_TEXT_LEN - strlen( text ) - 1 );
	}
}

static void get_frame_str( mlt_filter filter, mlt_frame frame, char* text )
{
	int pos = mlt_frame_get_position( frame );
	char s[12];
	snprintf( s, sizeof( s ) - 1, "%d", pos );
	strncat( text, s, MAX_TEXT_LEN - strlen( text ) - 1 );
}

static void get_filedate_str( mlt_filter filter, mlt_frame frame, char* text )
{
	mlt_producer producer = mlt_producer_cut_parent( mlt_frame_get_original_producer( frame ) );
	mlt_properties producer_properties = MLT_PRODUCER_PROPERTIES( producer );
	char* filename = mlt_properties_get( producer_properties, "resource");
	struct stat file_info;

	if( !stat(filename, &file_info))
	{
		struct tm* time_info = gmtime( &(file_info.st_mtime) );
		char date[11] = "";
		strftime( date, 11, "%Y/%m/%d", time_info );
		strncat( text, date, MAX_TEXT_LEN - strlen( text ) - 1);
	}
}

static void get_localfiledate_str( mlt_filter filter, mlt_frame frame, char* text )
{
	mlt_producer producer = mlt_producer_cut_parent( mlt_frame_get_original_producer( frame ) );
	mlt_properties producer_properties = MLT_PRODUCER_PROPERTIES( producer );
	char* filename = mlt_properties_get( producer_properties, "resource" );
	struct stat file_info;

	if( !stat( filename, &file_info ) )
	{
		struct tm* time_info = localtime( &(file_info.st_mtime) );
		char date[11] = "";
		strftime( date, 11, "%Y/%m/%d", time_info );
		strncat( text, date, MAX_TEXT_LEN - strlen( text ) - 1);
	}
}

static void get_resource_str( mlt_filter filter, mlt_frame frame, char* text )
{
	mlt_producer producer = mlt_producer_cut_parent( mlt_frame_get_original_producer( frame ) );
	mlt_properties producer_properties = MLT_PRODUCER_PROPERTIES( producer );
	strncat( text, mlt_properties_get( producer_properties, "resource" ), MAX_TEXT_LEN - strlen( text ) - 1 );
}

/** Perform substitution for keywords that are enclosed in "# #".
*/
static void substitute_keywords(mlt_filter filter, char* result, char* value, mlt_frame frame)
{
	char keyword[MAX_TEXT_LEN] = "";
	int pos = 0;
	int is_keyword = 0;

	while ( get_next_token(value, &pos, keyword, &is_keyword) )
	{
		if(!is_keyword)
		{
			strncat( result, keyword, MAX_TEXT_LEN - strlen( result ) - 1 );
		}
		else if ( !strcmp( keyword, "timecode" ) )
		{
			get_timecode_str( filter, frame, result );
		}
		else if ( !strcmp( keyword, "frame" ) )
		{
			get_frame_str( filter, frame, result );
		}
		else if ( !strcmp( keyword, "filedate" ) )
		{
			get_filedate_str( filter, frame, result );
		}
		else if ( !strcmp( keyword, "localfiledate" ) )
		{
			get_localfiledate_str( filter, frame, result );
		}
		else if ( !strcmp( keyword, "resource" ) )
		{
			get_resource_str( filter, frame, result );
		}
		else
		{
			// replace keyword with property value from this frame
			mlt_properties frame_properties = MLT_FRAME_PROPERTIES( frame );
			char *frame_value = mlt_properties_get( frame_properties, keyword );
			if( frame_value )
			{
				strncat( result, frame_value, MAX_TEXT_LEN - strlen(result) - 1 );
			}
		}
	}
}

static void setup_producer( mlt_filter filter, mlt_producer producer, mlt_frame frame )
{
	mlt_properties my_properties = MLT_FILTER_PROPERTIES( filter );
	mlt_properties producer_properties = MLT_PRODUCER_PROPERTIES( producer );
	char* dynamic_text = mlt_properties_get( my_properties, "argument" );

	// Check for keywords in dynamic text
	if ( dynamic_text )
	{
		// Apply keyword substitution before passing the text to the filter.
		char result[MAX_TEXT_LEN] = "";
		substitute_keywords( filter, result, dynamic_text, frame );
		mlt_properties_set( producer_properties, "markup", (char*)result );
	}

	// Pass the properties to the pango producer
	mlt_properties_set( producer_properties, "family", mlt_properties_get( my_properties, "family" ) );
	mlt_properties_set( producer_properties, "size", mlt_properties_get( my_properties, "size" ) );
	mlt_properties_set( producer_properties, "weight", mlt_properties_get( my_properties, "weight" ) );
	mlt_properties_set( producer_properties, "fgcolour", mlt_properties_get( my_properties, "fgcolour" ) );
	mlt_properties_set( producer_properties, "bgcolour", mlt_properties_get( my_properties, "bgcolour" ) );
	mlt_properties_set( producer_properties, "olcolour", mlt_properties_get( my_properties, "olcolour" ) );
	mlt_properties_set( producer_properties, "pad", mlt_properties_get( my_properties, "pad" ) );
	mlt_properties_set( producer_properties, "outline", mlt_properties_get( my_properties, "outline" ) );
	mlt_properties_set( producer_properties, "align", mlt_properties_get( my_properties, "halign" ) );
}

static void setup_transition( mlt_filter filter, mlt_transition transition )
{
	mlt_properties my_properties = MLT_FILTER_PROPERTIES( filter );
	mlt_properties transition_properties = MLT_TRANSITION_PROPERTIES( transition );

	mlt_properties_set( transition_properties, "geometry", mlt_properties_get( my_properties, "geometry" ) );
	mlt_properties_set( transition_properties, "halign", mlt_properties_get( my_properties, "halign" ) );
	mlt_properties_set( transition_properties, "valign", mlt_properties_get( my_properties, "valign" ) );
	mlt_properties_set_int( transition_properties, "out", mlt_properties_get_int( my_properties, "_out" ) );
	mlt_properties_set_int( transition_properties, "refresh", 1 );
}


/** Get the image.
*/
static int filter_get_image( mlt_frame frame, uint8_t **image, mlt_image_format *format, int *width, int *height, int writable )
{
	int error = 0;
	mlt_filter filter = mlt_frame_pop_service( frame );
	mlt_properties properties = MLT_FILTER_PROPERTIES( filter );
	mlt_producer producer = mlt_properties_get_data( properties, "_producer", NULL );
	mlt_transition transition = mlt_properties_get_data( properties, "_transition", NULL );
	mlt_frame text_frame = NULL;
	mlt_position position = 0;

	// Configure this filter
	mlt_service_lock( MLT_FILTER_SERVICE( filter ) );
	setup_producer( filter, producer, frame );
	setup_transition( filter, transition );
	mlt_service_unlock( MLT_FILTER_SERVICE( filter ) );

	// Make sure the producer is in the correct position
	position = mlt_filter_get_position( filter, frame );
	mlt_producer_seek( producer, position );

	// Get the b frame and process with transition if successful
	if ( mlt_service_get_frame( MLT_PRODUCER_SERVICE( producer ), &text_frame, 0 ) == 0 )
	{
		// Get the a and b frame properties
		mlt_properties a_props = MLT_FRAME_PROPERTIES( frame );
		mlt_properties b_props = MLT_FRAME_PROPERTIES( text_frame );

		// Set the frame and text_frame to be in the same position and have same consumer requirements
		mlt_frame_set_position( text_frame, position );
		mlt_frame_set_position( frame, position );
		mlt_properties_set_int( b_props, "consumer_deinterlace", mlt_properties_get_int( a_props, "consumer_deinterlace" ) );

		// Apply all filters that are attached to this filter to the b frame
		mlt_service_apply_filters( MLT_FILTER_SERVICE( filter ), text_frame, 0 );

		// Process the frame
		mlt_transition_process( transition, frame, text_frame );

		// Get the image
		*format = mlt_image_yuv422;
		error = mlt_frame_get_image( frame, image, format, width, height, 1 );

		// Close the b frame
		mlt_frame_close( text_frame );
	}

	return error;
}

/** Filter processing.
*/
static mlt_frame filter_process( mlt_filter filter, mlt_frame frame )
{
	// Get the properties of the frame
	mlt_properties properties = MLT_FRAME_PROPERTIES( frame );

	// Save the frame out point
	mlt_properties_set_int( MLT_FILTER_PROPERTIES( filter ), "_out", mlt_properties_get_int( properties, "out" ) );

	// Push the filter on to the stack
	mlt_frame_push_service( frame, filter );

	// Push the get_image on to the stack
	mlt_frame_push_get_image( frame, filter_get_image );

	return frame;
}

/** Constructor for the filter.
*/
mlt_filter filter_dynamictext_init( mlt_profile profile, mlt_service_type type, const char *id, char *arg )
{
	mlt_filter filter = mlt_filter_new();
	mlt_transition transition = mlt_factory_transition( profile, "composite", NULL );
	mlt_producer producer = mlt_factory_producer( profile, mlt_environment( "MLT_PRODUCER" ), "pango:" );

	if ( filter && transition && producer )
	{
		mlt_properties my_properties = MLT_FILTER_PROPERTIES( filter );

		// Register the transition for reuse/destruction
    	mlt_properties_set_data( my_properties, "_transition", transition, 0, ( mlt_destructor )mlt_transition_close, NULL );

		// Register the producer for reuse/destruction
		mlt_properties_set_data( my_properties, "_producer", producer, 0, ( mlt_destructor )mlt_producer_close, NULL );

		// Ensure that we loop
		mlt_properties_set( MLT_PRODUCER_PROPERTIES( producer ), "eof", "loop" );

		// Assign default values
		mlt_properties_set( my_properties, "argument", arg ? arg: "#timecode#" );
		mlt_properties_set( my_properties, "geometry", "0%/0%:100%x100%:100" );
		mlt_properties_set( my_properties, "family", "Sans" );
		mlt_properties_set( my_properties, "size", "48" );
		mlt_properties_set( my_properties, "weight", "400" );
		mlt_properties_set( my_properties, "fgcolour", "0x000000ff" );
		mlt_properties_set( my_properties, "bgcolour", "0x00000020" );
		mlt_properties_set( my_properties, "olcolour", "0x00000000" );
		mlt_properties_set( my_properties, "pad", "0" );
		mlt_properties_set( my_properties, "halign", "left" );
		mlt_properties_set( my_properties, "valign", "top" );
		mlt_properties_set( my_properties, "outline", "0" );

		mlt_properties_set_int( my_properties, "_filter_private", 1 );

		filter->process = filter_process;
	}
	else
	{
		if( filter )
		{
			mlt_filter_close( filter );
		}

		if( transition )
		{
			mlt_transition_close( transition );
		}

		if( producer )
		{
			mlt_producer_close( producer );
		}

		filter = NULL;
	}
	return filter;
}
