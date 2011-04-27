/**
 * MltTransition.h - MLT Wrapper
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

#ifndef _MLTPP_TRANSITION_H_
#define _MLTPP_TRANSITION_H_

#include "config.h"

#include <framework/mlt.h>
#include "MltService.h"

namespace Mlt
{
	class Service;
	class Profile;
	class Frame;

	class MLTPP_DECLSPEC Transition : public Service
	{
		private:
			mlt_transition instance;
		public:
			Transition( Profile& profile, const char *id, const char *arg = NULL );
			Transition( Service &transition );
			Transition( Transition &transition );
			Transition( mlt_transition transition );
			virtual ~Transition( );
			virtual mlt_transition get_transition( );
			mlt_service get_service( );
			void set_in_and_out( int in, int out );
			int connect( Producer &producer, int a_track, int b_track );
			int get_a_track( );
			int get_b_track( );
			int get_in( );
			int get_out( );
			int get_length( );
			int get_position( Frame &frame );
			double get_progress( Frame &frame );
			double get_progress_delta( Frame &frame );
	};
}

#endif
