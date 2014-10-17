/*
 * factory.c -- the factory method interfaces
 * Copyright (C) 2006 Visual Media
 * Author: Charles Yates <charles.yates@gmail.com>
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

#include <string.h>
#include <limits.h>
#include <framework/mlt.h>

extern mlt_consumer consumer_qglsl_init( mlt_profile profile, mlt_service_type type, const char *id, char *arg );
extern mlt_producer producer_qimage_init( mlt_profile profile, mlt_service_type type, const char *id, char *arg );
extern mlt_producer producer_qtext_init( mlt_profile profile, mlt_service_type type, const char *id, char *arg );
extern mlt_producer producer_kdenlivetitle_init( mlt_profile profile, mlt_service_type type, const char *id, char *arg );
extern mlt_transition transition_vqm_init( mlt_profile profile, mlt_service_type type, const char *id, void *arg );

static mlt_properties metadata( mlt_service_type type, const char *id, void *data )
{
	char file[ PATH_MAX ];
	snprintf( file, PATH_MAX, "%s/qt/%s", mlt_environment( "MLT_DATA" ), (char*) data );
	return mlt_properties_parse_yaml( file );
}

MLT_REPOSITORY
{
	MLT_REGISTER( consumer_type, "qglsl", consumer_qglsl_init );
	MLT_REGISTER( producer_type, "qimage", producer_qimage_init );
	MLT_REGISTER( producer_type, "qtext", producer_qtext_init );
	MLT_REGISTER( producer_type, "kdenlivetitle", producer_kdenlivetitle_init );
	MLT_REGISTER_METADATA( producer_type, "qimage", metadata, "producer_qimage.yml" );
	MLT_REGISTER_METADATA( producer_type, "qtext", metadata, "producer_qtext.yml" );
	MLT_REGISTER_METADATA( producer_type, "kdenlivetitle", metadata, "producer_kdenlivetitle.yml" );
#ifdef GPL3
	MLT_REGISTER( transition_type, "vqm", transition_vqm_init );
	MLT_REGISTER_METADATA( transition_type, "vqm", metadata, "transition_vqm.yml" );
#endif
}
