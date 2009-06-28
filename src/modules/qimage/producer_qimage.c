/*
 * producer_image.c -- a QT/QImage based producer for MLT
 * Copyright (C) 2006 Visual Media
 * Author: Charles Yates <charles.yates@gmail.com>
 *
 * NB: This module is designed to be functionally equivalent to the 
 * gtk2 image loading module so it can be used as replacement.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <framework/mlt_producer.h>
#include <framework/mlt_cache.h>
#include "qimage_wrapper.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

static void load_filenames( producer_qimage this, mlt_properties producer_properties );
static int producer_get_frame( mlt_producer parent, mlt_frame_ptr frame, int index );
static void producer_close( mlt_producer parent );

mlt_producer producer_qimage_init( mlt_profile profile, mlt_service_type type, const char *id, char *filename )
{
	producer_qimage this = calloc( sizeof( struct producer_qimage_s ), 1 );
	if ( this != NULL && mlt_producer_init( &this->parent, this ) == 0 )
	{
		mlt_producer producer = &this->parent;

		// Get the properties interface
		mlt_properties properties = MLT_PRODUCER_PROPERTIES( &this->parent );
	
		// Callback registration
#ifdef USE_KDE
		init_qimage();
#endif
		producer->get_frame = producer_get_frame;
		producer->close = ( mlt_destructor )producer_close;

		// Set the default properties
		mlt_properties_set( properties, "resource", filename );
		mlt_properties_set_int( properties, "ttl", 25 );
		mlt_properties_set_int( properties, "aspect_ratio", 1 );
		mlt_properties_set_int( properties, "progressive", 1 );
		
		// Validate the resource
		if ( filename )
			load_filenames( this, properties );
		if ( this->count )
		{
			mlt_frame frame = mlt_frame_init( MLT_PRODUCER_SERVICE( producer ) );
			if ( frame )
			{
				mlt_properties frame_properties = MLT_FRAME_PROPERTIES( frame );
				pthread_mutex_init( &this->mutex, NULL );
				mlt_properties_set_data( frame_properties, "producer_qimage", this, 0, NULL, NULL );
				mlt_frame_set_position( frame, mlt_producer_position( producer ) );
				mlt_properties_set_position( frame_properties, "qimage_position", mlt_producer_position( producer ) );
				refresh_qimage( this, frame, 0, 0 );
				mlt_frame_close( frame );
			}
		}
		if ( this->current_width == 0 )
		{
			producer_close( producer );
			producer = NULL;
		}
		return producer;
	}
	free( this );
	return NULL;
}

static void load_filenames( producer_qimage this, mlt_properties producer_properties )
{
	char *filename = mlt_properties_get( producer_properties, "resource" );
	this->filenames = mlt_properties_new( );

	// Read xml string
	if ( strstr( filename, "<svg" ) )
	{
		// Generate a temporary file for the svg
		char fullname[ 1024 ] = "/tmp/mlt.XXXXXX";
		int fd = mkstemp( fullname );

		if ( fd > -1 )
		{
			// Write the svg into the temp file
			ssize_t remaining_bytes;
			char *xml = filename;
			
			// Strip leading crap
			while ( xml[0] != '<' )
				xml++;
			
			remaining_bytes = strlen( xml );
			while ( remaining_bytes > 0 )
				remaining_bytes -= write( fd, xml + strlen( xml ) - remaining_bytes, remaining_bytes );
			close( fd );

			mlt_properties_set( this->filenames, "0", fullname );

			// Teehe - when the producer closes, delete the temp file and the space allo
			mlt_properties_set_data( producer_properties, "__temporary_file__", fullname, 0, ( mlt_destructor )unlink, NULL );
		}
	}
	// Obtain filenames
	else if ( strchr( filename, '%' ) != NULL )
	{
		// handle picture sequences
		int i = mlt_properties_get_int( producer_properties, "begin" );
		int gap = 0;
		char full[1024];
		int keyvalue = 0;
		char key[ 50 ];

		while ( gap < 100 )
		{
			struct stat buf;
			snprintf( full, 1023, filename, i ++ );
			if ( stat( full, &buf ) == 0 )
			{
				sprintf( key, "%d", keyvalue ++ );
				mlt_properties_set( this->filenames, key, full );
				gap = 0;
			}
			else
			{
				gap ++;
			}
		}
		if ( mlt_properties_count( this->filenames ) > 0 )
			mlt_properties_set_int( producer_properties, "ttl", 1 );
	}
	else if ( strstr( filename, "/.all." ) != NULL )
	{
		char wildcard[ 1024 ];
		char *dir_name = strdup( filename );
		char *extension = strrchr( dir_name, '.' );

		*( strstr( dir_name, "/.all." ) + 1 ) = '\0';
		sprintf( wildcard, "*%s", extension );

		mlt_properties_dir_list( this->filenames, dir_name, wildcard, 1 );

		free( dir_name );
	}
	else
	{
		mlt_properties_set( this->filenames, "0", filename );
	}

	this->count = mlt_properties_count( this->filenames );
}

static int producer_get_image( mlt_frame frame, uint8_t **buffer, mlt_image_format *format, int *width, int *height, int writable )
{
	// Obtain properties of frame
	mlt_properties properties = MLT_FRAME_PROPERTIES( frame );

	// Obtain the producer for this frame
	producer_qimage this = mlt_properties_get_data( properties, "producer_qimage", NULL );

	*width = mlt_properties_get_int( properties, "rescale_width" );
	*height = mlt_properties_get_int( properties, "rescale_height" );

	// Refresh the image
	refresh_qimage( this, frame, *width, *height );

	// We need to know the size of the image to clone it
	int image_size = this->current_width * ( this->current_height + 1 ) * 2;
	int alpha_size = this->current_width * this->current_height;

	// Get width and height (may have changed during the refresh)
	*width = mlt_properties_get_int( properties, "width" );
	*height = mlt_properties_get_int( properties, "height" );

	// NB: Cloning is necessary with this producer (due to processing of images ahead of use)
	// The fault is not in the design of mlt, but in the implementation of the qimage producer...
	if ( this->current_image != NULL )
	{
		if ( *format == mlt_image_yuv422 || *format == mlt_image_yuv420p )
		{
			// Clone the image and the alpha
			uint8_t *image_copy = mlt_pool_alloc( image_size );
			uint8_t *alpha_copy = mlt_pool_alloc( alpha_size );

			memcpy( image_copy, this->current_image, image_size );

			// Copy or default the alpha
			if ( this->current_alpha )
				memcpy( alpha_copy, this->current_alpha, alpha_size );
			else
				memset( alpha_copy, 255, alpha_size );

			// Now update properties so we free the copy after
			mlt_properties_set_data( properties, "image", image_copy, image_size, mlt_pool_release, NULL );
			mlt_properties_set_data( properties, "alpha", alpha_copy, alpha_size, mlt_pool_release, NULL );

			// We're going to pass the copy on
			*buffer = image_copy;
		}
		else if ( *format == mlt_image_rgb24a )
		{
			// Clone the image and the alpha
			image_size = *width * ( *height + 1 ) * 4;
			alpha_size = *width * ( *height + 1 );
			uint8_t *image_copy = mlt_pool_alloc( image_size );
			uint8_t *alpha_copy = mlt_pool_alloc( alpha_size );

			mlt_convert_yuv422_to_rgb24a(this->current_image, image_copy, (*width)*(*height));

			// Now update properties so we free the copy after
			mlt_properties_set_data( properties, "image", image_copy, image_size, mlt_pool_release, NULL );
			mlt_properties_set_data( properties, "alpha", alpha_copy, alpha_size, mlt_pool_release, NULL );

			// We're going to pass the copy on
			*buffer = image_copy;
		}
	}
	else
	{
		// TODO: Review all cases of invalid images
		*buffer = mlt_pool_alloc( 50 * 50 * 2 );
		mlt_properties_set_data( properties, "image", *buffer, image_size, mlt_pool_release, NULL );
		*width = 50;
		*height = 50;
	}

	// Release references and locks
	pthread_mutex_unlock( &this->mutex );
	mlt_cache_item_close( this->image_cache );
	mlt_cache_item_close( this->alpha_cache );

	return 0;
}

static uint8_t *producer_get_alpha_mask( mlt_frame this )
{
	// Obtain properties of frame
	mlt_properties properties = MLT_FRAME_PROPERTIES( this );

	// Return the alpha mask
	return mlt_properties_get_data( properties, "alpha", NULL );
}

static int producer_get_frame( mlt_producer producer, mlt_frame_ptr frame, int index )
{
	// Get the real structure for this producer
	producer_qimage this = producer->child;

	// Fetch the producers properties
	mlt_properties producer_properties = MLT_PRODUCER_PROPERTIES( producer );

	if ( this->filenames == NULL && mlt_properties_get( producer_properties, "resource" ) != NULL )
		load_filenames( this, producer_properties );

	// Generate a frame
	*frame = mlt_frame_init( MLT_PRODUCER_SERVICE( producer ) );

	if ( *frame != NULL && this->count > 0 )
	{
		// Obtain properties of frame and producer
		mlt_properties properties = MLT_FRAME_PROPERTIES( *frame );

		// Set the producer on the frame properties
		mlt_properties_set_data( properties, "producer_qimage", this, 0, NULL, NULL );

		// Update timecode on the frame we're creating
		mlt_frame_set_position( *frame, mlt_producer_position( producer ) );

		// Ensure that we have a way to obtain the position in the get_image
		mlt_properties_set_position( properties, "qimage_position", mlt_producer_position( producer ) );

		// Refresh the image
		refresh_qimage( this, *frame, 0, 0 );

		// Set producer-specific frame properties
		mlt_properties_set_int( properties, "progressive", mlt_properties_get_int( producer_properties, "progressive" ) );
		mlt_properties_set_double( properties, "aspect_ratio", mlt_properties_get_double( producer_properties, "aspect_ratio" ) );

		// Set alpha call back
		( *frame )->get_alpha_mask = producer_get_alpha_mask;

		// Push the get_image method
		mlt_frame_push_get_image( *frame, producer_get_image );
	}

	// Calculate the next timecode
	mlt_producer_prepare_next( producer );

	return 0;
}

static void producer_close( mlt_producer parent )
{
	producer_qimage this = parent->child;
	pthread_mutex_destroy( &this->mutex );
	parent->close = NULL;
	mlt_producer_close( parent );
	mlt_properties_close( this->filenames );
	free( this );
}
