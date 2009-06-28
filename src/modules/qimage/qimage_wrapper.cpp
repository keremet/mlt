/*
 * qimage_wrapper.cpp -- a QT/QImage based producer for MLT
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

#include "qimage_wrapper.h"

#ifdef USE_QT3
#include <qimage.h>
#include <qmutex.h>

#ifdef USE_KDE
#include <kinstance.h>
#include <kimageio.h>
#endif

#endif


#ifdef USE_QT4
#include <QtGui/QImage>
#include <QtCore/QSysInfo>
#include <QtCore/QMutex>
#endif


#include <cmath>

extern "C" {

#include <framework/mlt_pool.h>
#include <framework/mlt_cache.h>

#ifdef USE_KDE
static KInstance *instance = 0L;
#endif

static void qimage_delete( void *data )
{
	QImage *image = ( QImage * )data;
	delete image;
	image = NULL;
#ifdef USE_KDE
	if (instance) delete instance;
	instance = 0L;
#endif
}

static QMutex g_mutex;

#ifdef USE_KDE
void init_qimage()
{
	if (!instance) {
	    instance = new KInstance("qimage_prod");
	    KImageIO::registerFormats();
	}
}
#endif

void refresh_qimage( producer_qimage self, mlt_frame frame, int width, int height )
{
	// Obtain properties of frame
	mlt_properties properties = MLT_FRAME_PROPERTIES( frame );

	// Obtain the producer 
	mlt_producer producer = &self->parent;

	// Obtain properties of producer
	mlt_properties producer_props = MLT_PRODUCER_PROPERTIES( producer );

	// restore QImage
	pthread_mutex_lock( &self->mutex );
	mlt_cache_item qimage_cache = mlt_service_cache_get( MLT_PRODUCER_SERVICE( producer ), "qimage.qimage" );
	QImage *qimage = static_cast<QImage*>( mlt_cache_item_data( qimage_cache, NULL ) );

	// restore scaled image
	self->image_cache = mlt_service_cache_get( MLT_PRODUCER_SERVICE( producer ), "qimage.image" );
	self->current_image = static_cast<uint8_t*>( mlt_cache_item_data( self->image_cache, NULL ) );

	// restore alpha channel
	self->alpha_cache = mlt_service_cache_get( MLT_PRODUCER_SERVICE( producer ), "qimage.alpha" );
	self->current_alpha = static_cast<uint8_t*>( mlt_cache_item_data( self->alpha_cache, NULL ) );

	// Check if user wants us to reload the image
	if ( mlt_properties_get_int( producer_props, "force_reload" ) ) 
	{
		qimage = NULL;
		self->current_image = NULL;
		mlt_properties_set_int( producer_props, "force_reload", 0 );
	}

	// Obtain the cache flag and structure
	int use_cache = mlt_properties_get_int( producer_props, "cache" );
	mlt_properties cache = ( mlt_properties )mlt_properties_get_data( producer_props, "_cache", NULL );
	int update_cache = 0;

	// Get the time to live for each frame
	double ttl = mlt_properties_get_int( producer_props, "ttl" );

	// Get the original position of this frame
	mlt_position position = mlt_properties_get_position( properties, "qimage_position" );
	position += mlt_producer_get_in( producer );

	// Image index
	int image_idx = ( int )floor( ( double )position / ttl ) % self->count;

	// Key for the cache
	char image_key[ 10 ];
	sprintf( image_key, "%d", image_idx );

	g_mutex.lock();

	// Check if the frame is already loaded
	if ( use_cache )
	{
		if ( cache == NULL )
		{
			cache = mlt_properties_new( );
			mlt_properties_set_data( producer_props, "_cache", cache, 0, ( mlt_destructor )mlt_properties_close, NULL );
		}

		mlt_frame cached = ( mlt_frame )mlt_properties_get_data( cache, image_key, NULL );

		if ( cached )
		{
			self->image_idx = image_idx;
			mlt_properties cached_props = MLT_FRAME_PROPERTIES( cached );
			self->current_width = mlt_properties_get_int( cached_props, "width" );
			self->current_height = mlt_properties_get_int( cached_props, "height" );
			mlt_properties_set_int( producer_props, "_real_width", mlt_properties_get_int( cached_props, "real_width" ) );
			mlt_properties_set_int( producer_props, "_real_height", mlt_properties_get_int( cached_props, "real_height" ) );
			self->current_image = ( uint8_t * )mlt_properties_get_data( cached_props, "image", NULL );
			self->current_alpha = ( uint8_t * )mlt_properties_get_data( cached_props, "alpha", NULL );

			if ( width != 0 && ( width != self->current_width || height != self->current_height ) )
				self->current_image = NULL;
		}
	}

    // optimization for subsequent iterations on single picture
	if ( width != 0 && ( image_idx != self->image_idx || width != self->current_width || height != self->current_height ) )
		self->current_image = NULL;
	if ( image_idx != self->qimage_idx )
		qimage = NULL;
	if ( !qimage && !self->current_image )
	{
		self->current_image = NULL;
		qimage = new QImage( mlt_properties_get_value( self->filenames, image_idx ) );

		if ( !qimage->isNull( ) )
		{
			// Store the width/height of the qimage
			self->current_width = qimage->width( );
			self->current_height = qimage->height( );

			// Register qimage for destruction and reuse
			mlt_cache_item_close( qimage_cache );
			mlt_service_cache_put( MLT_PRODUCER_SERVICE( producer ), "qimage.qimage", qimage, 0, ( mlt_destructor )qimage_delete );
			qimage_cache = mlt_service_cache_get( MLT_PRODUCER_SERVICE( producer ), "qimage.qimage" );
			self->qimage_idx = image_idx;

			mlt_events_block( producer_props, NULL );
			mlt_properties_set_int( producer_props, "_real_width", self->current_width );
			mlt_properties_set_int( producer_props, "_real_height", self->current_height );
			mlt_events_unblock( producer_props, NULL );
		}
		else
		{
			delete qimage;
			qimage = NULL;
		}
	}

	// If we have a pixbuf and this request specifies a valid dimension and we haven't already got a cached version...
	if ( qimage && width > 0 && !self->current_image )
	{
		char *interps = mlt_properties_get( properties, "rescale.interp" );
		int interp = 0;

		// QImage has two scaling modes - we'll toggle between them here
		if ( strcmp( interps, "tiles" ) == 0 )
			interp = 1;
		else if ( strcmp( interps, "hyper" ) == 0 )
			interp = 1;

#ifdef USE_QT4
		// Note - the original qimage is already safe and ready for destruction
		QImage scaled = interp == 0 ? qimage->scaled( QSize( width, height)) : qimage->scaled( QSize(width, height), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
		QImage temp;
		bool hasAlpha = scaled.hasAlphaChannel();
		if (hasAlpha)
		    temp = scaled.convertToFormat(QImage::Format_ARGB32);
		else 
		    temp = scaled.convertToFormat(QImage::Format_RGB888);
#endif

#ifdef USE_QT3
		// Note - the original qimage is already safe and ready for destruction
		QImage scaled = interp == 0 ? qimage->scale( width, height, QImage::ScaleFree ) : qimage->smoothScale( width, height, QImage::ScaleFree );
		QImage temp = scaled.convertDepth( 32 );
		bool hasAlpha = true;
#endif

		// Store width and height
		self->current_width = width;
		self->current_height = height;
		
		// Allocate/define image
		self->current_image = ( uint8_t * )mlt_pool_alloc( width * ( height + 1 ) * 2 );
		if ( !use_cache )
			mlt_cache_item_close( self->image_cache );
		mlt_service_cache_put( MLT_PRODUCER_SERVICE( producer ), "qimage.image", self->current_image, width * ( height + 1 ) * 2, mlt_pool_release );
		self->image_cache = mlt_service_cache_get( MLT_PRODUCER_SERVICE( producer ), "qimage.image" );
		self->image_idx = image_idx;

		if (!hasAlpha) {
			mlt_convert_rgb24_to_yuv422( temp.bits(), self->current_width, self->current_height, temp.bytesPerLine(), self->current_image ); 
		}
		else {
			// Allocate the alpha mask
			self->current_alpha = ( uint8_t * )mlt_pool_alloc( width * height );
			if ( !use_cache )
				mlt_cache_item_close( self->alpha_cache );
			mlt_service_cache_put( MLT_PRODUCER_SERVICE( producer ), "qimage.alpha", self->current_alpha, width * height, mlt_pool_release );
			self->alpha_cache = mlt_service_cache_get( MLT_PRODUCER_SERVICE( producer ), "qimage.alpha" );

#ifdef USE_QT4
			if ( QSysInfo::ByteOrder == QSysInfo::BigEndian )
				mlt_convert_argb_to_yuv422( temp.bits( ), self->current_width, self->current_height, temp.bytesPerLine(), self->current_image, self->current_alpha );
			else
				mlt_convert_bgr24a_to_yuv422( temp.bits( ), self->current_width, self->current_height, temp.bytesPerLine( ), self->current_image, self->current_alpha );
#endif

#ifdef USE_QT3
			// Convert the image
			if ( QImage::systemByteOrder( ) == QImage::BigEndian )
				mlt_convert_argb_to_yuv422( temp.bits( ), self->current_width, self->current_height, temp.bytesPerLine( ), self->current_image, self->current_alpha );
			else
				mlt_convert_bgr24a_to_yuv422( temp.bits( ), self->current_width, self->current_height, temp.bytesPerLine( ), self->current_image, self->current_alpha );
#endif
		}

		// Ensure we update the cache when we need to
		update_cache = use_cache;
	}

	// release references no longer needed
	mlt_cache_item_close( qimage_cache );
	if ( width == 0 )
	{
		pthread_mutex_unlock( &self->mutex );
		mlt_cache_item_close( self->image_cache );
		mlt_cache_item_close( self->alpha_cache );
	}

	// Set width/height of frame
	mlt_properties_set_int( properties, "width", self->current_width );
	mlt_properties_set_int( properties, "height", self->current_height );
	mlt_properties_set_int( properties, "real_width", mlt_properties_get_int( producer_props, "_real_width" ) );
	mlt_properties_set_int( properties, "real_height", mlt_properties_get_int( producer_props, "_real_height" ) );

	if ( update_cache )
	{
		mlt_frame cached = mlt_frame_init( MLT_PRODUCER_SERVICE( producer ) );
		mlt_properties cached_props = MLT_FRAME_PROPERTIES( cached );
		mlt_properties_set_int( cached_props, "width", self->current_width );
		mlt_properties_set_int( cached_props, "height", self->current_height );
		mlt_properties_set_int( cached_props, "real_width", mlt_properties_get_int( producer_props, "_real_width" ) );
		mlt_properties_set_int( cached_props, "real_height", mlt_properties_get_int( producer_props, "_real_height" ) );
		mlt_properties_set_data( cached_props, "image", self->current_image, self->current_width * ( self->current_height + 1 ) * 2, mlt_pool_release, NULL );
		mlt_properties_set_data( cached_props, "alpha", self->current_alpha, self->current_width * self->current_height, mlt_pool_release, NULL );
		mlt_properties_set_data( cache, image_key, cached, 0, ( mlt_destructor )mlt_frame_close, NULL );
	}
	g_mutex.unlock();
}

} // extern "C"
