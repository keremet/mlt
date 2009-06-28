/**
 * MltTokeniser.cpp - MLT Wrapper
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

#include <stdlib.h>
#include "MltTokeniser.h"
using namespace Mlt;

Tokeniser::Tokeniser( char *text, char *delimiter )
{
	tokens = mlt_tokeniser_init( );
	if ( text != NULL )
		mlt_tokeniser_parse_new( tokens, text, delimiter );
}

Tokeniser::~Tokeniser( )
{
	mlt_tokeniser_close( tokens );
}

int Tokeniser::parse( char *text, char *delimiter )
{
	return mlt_tokeniser_parse_new( tokens, text, delimiter );
}

int Tokeniser::count( )
{
	return mlt_tokeniser_count( tokens );
}

char *Tokeniser::get( int index )
{
	return mlt_tokeniser_get_string( tokens, index );
}

char *Tokeniser::input( )
{
	return mlt_tokeniser_get_input( tokens );
}

