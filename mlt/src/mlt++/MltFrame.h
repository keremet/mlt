/**
 * MltFilter.h - MLT Wrapper
 * Copyright (C) 2004-2005 Charles Yates
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

#ifndef _MLTPP_FRAME_H_
#define _MLTPP_FRAME_H_

#include "config.h"

#include <framework/mlt.h>
#include "MltProperties.h"

namespace Mlt
{
	class Properties;
	class Producer;
	class Service;

	class MLTPP_DECLSPEC Frame : public Properties
	{
		private:
			mlt_frame instance;
		public:
			Frame( mlt_frame frame );
			Frame( Frame &frame );
			virtual ~Frame( );
			virtual mlt_frame get_frame( );
			mlt_properties get_properties( );
			uint8_t *get_image( mlt_image_format &format, int &w, int &h, int writable = 0 );
			unsigned char *fetch_image( mlt_image_format format, int w, int h, int writable = 0 );
			void *get_audio( mlt_audio_format &format, int &frequency, int &channels, int &samples );
			unsigned char *get_waveform( int w, int h );
			Producer *get_original_producer( );
			int get_position( );
			mlt_properties get_unique_properties( Service &service );
			int set_image( uint8_t *image, int size, mlt_destructor destroy );
			int set_alpha( uint8_t *alpha, int size, mlt_destructor destroy );
	};
}

#endif
