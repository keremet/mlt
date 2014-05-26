/*
 * filter_movit_crop.cpp
 * Copyright (C) 2013 Dan Dennedy <dan@dennedy.org>
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

#include <framework/mlt.h>
#include <string.h>
#include <assert.h>

#include "glsl_manager.h"
#include <movit/padding_effect.h>

static int get_image( mlt_frame frame, uint8_t **image, mlt_image_format *format, int *width, int *height, int writable )
{
	int error = 0;
	mlt_properties properties = MLT_FRAME_PROPERTIES( frame );
	mlt_filter filter = (mlt_filter) mlt_frame_pop_service( frame );
	mlt_profile profile = mlt_service_profile( MLT_FILTER_SERVICE( filter ) );
	mlt_image_format requested_format = *format;

	// Correct width/height if necessary
	*width = mlt_properties_get_int( properties, "crop.original_width" );
	*height = mlt_properties_get_int( properties, "crop.original_height" );
	if ( *width == 0 || *height == 0 )
	{
		*width = mlt_properties_get_int( properties, "meta.media.width" );
		*height = mlt_properties_get_int( properties, "meta.media.height" );
	}
	if ( *width == 0 || *height == 0 )
	{
		*width = profile->width;
		*height = profile->height;
	}
	mlt_properties_set_int( properties, "rescale_width", *width );
	mlt_properties_set_int( properties, "rescale_height", *height );

	// Get the image as requested
//	*format = (mlt_image_format) mlt_properties_get_int( MLT_PRODUCER_PROPERTIES(producer), "_movit image_format" );
	*format = mlt_image_none;
	if ( mlt_properties_get_int( properties, "test_image" ) )
		*format = mlt_image_yuv422;
	error = mlt_frame_get_image( frame, image, format, width, height, writable );

	// Skip processing if requested.
	if ( requested_format == mlt_image_none )
		return error;

	if ( !error && *format != mlt_image_glsl && frame->convert_image ) {
	// Pin the requested format to the first one returned.
//		mlt_properties_set_int( MLT_PRODUCER_PROPERTIES(producer), "_movit image_format", *format );
		error = frame->convert_image( frame, image, format, mlt_image_glsl );
	}
	if ( !error ) {
		double left    = mlt_properties_get_double( properties, "crop.left" );
		double right   = mlt_properties_get_double( properties, "crop.right" );
		double top     = mlt_properties_get_double( properties, "crop.top" );
		double bottom  = mlt_properties_get_double( properties, "crop.bottom" );
		int owidth  = *width - left - right;
		int oheight = *height - top - bottom;
		owidth = owidth < 0 ? 0 : owidth;
		oheight = oheight < 0 ? 0 : oheight;

		mlt_log_debug( MLT_FILTER_SERVICE(filter), "%dx%d -> %dx%d\n", *width, *height, owidth, oheight);

		GlslManager::get_instance()->lock_service( frame );
		Effect* effect = GlslManager::get_effect( filter, frame );
		if ( effect ) {
			bool ok = effect->set_int( "width", owidth );
			ok |= effect->set_int( "height", oheight );
			ok |= effect->set_float( "left", -left );
			ok |= effect->set_float( "top", -top );
			assert(ok);
			*width = owidth;
			*height = oheight;
		}
		GlslManager::get_instance()->unlock_service( frame );
	}

	return error;
}

static mlt_frame process( mlt_filter filter, mlt_frame frame )
{
	mlt_producer producer = mlt_producer_cut_parent( mlt_frame_get_original_producer( frame ) );
	if ( !GlslManager::init_chain( MLT_PRODUCER_SERVICE(producer) ) ) {
		Effect* effect = GlslManager::add_effect( filter, frame, new PaddingEffect );
		RGBATuple border_color( 0.0f, 0.0f, 0.0f, 1.0f );
		bool ok = effect->set_vec4( "border_color", (float*) &border_color );
		assert(ok);
	}
	mlt_frame_push_service( frame, filter );
	mlt_frame_push_get_image( frame, get_image );
	return frame;
}

extern "C"
mlt_filter filter_movit_crop_init( mlt_profile profile, mlt_service_type type, const char *id, char *arg )
{
	mlt_filter filter = NULL;
	GlslManager* glsl = GlslManager::get_instance();

	if ( glsl && ( filter = mlt_filter_new() ) ) {
		filter->process = process;
	}
	return filter;
}
