/*
 * producer_avformat.c -- avformat producer
 * Copyright (C) 2003-2004 Ushodaya Enterprises Limited
 * Author: Charles Yates <charles.yates@pandora.be>
 * Much code borrowed from ffmpeg.c: Copyright (c) 2000-2003 Fabrice Bellard
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

// MLT Header files
#include <framework/mlt_producer.h>
#include <framework/mlt_frame.h>
#include <framework/mlt_profile.h>
#include <framework/mlt_log.h>

// ffmpeg Header files
#include <avformat.h>
#include <opt.h>
#ifdef SWSCALE
#  include <swscale.h>
#endif
#if (LIBAVCODEC_VERSION_INT >= ((51<<16)+(71<<8)+0))
#  include "audioconvert.h"
#endif

// System header files
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#if LIBAVUTIL_VERSION_INT < (50<<16)
#define PIX_FMT_RGB32 PIX_FMT_RGBA32
#define PIX_FMT_YUYV422 PIX_FMT_YUV422
#endif

#define POSITION_INITIAL (-2)
#define POSITION_INVALID (-1)

void avformat_lock( );
void avformat_unlock( );

// Forward references.
static int producer_open( mlt_producer this, mlt_profile profile, char *file );
static int producer_get_frame( mlt_producer this, mlt_frame_ptr frame, int index );

/** Constructor for libavformat.
*/

mlt_producer producer_avformat_init( mlt_profile profile, char *file )
{
	int error = 0;

	// Report information about available demuxers and codecs as YAML Tiny
	if ( file && strstr( file, "f-list" ) )
	{
		fprintf( stderr, "---\nformats:\n" );
		AVInputFormat *format = NULL;
		while ( ( format = av_iformat_next( format ) ) )
			fprintf( stderr, "  - %s\n", format->name );
		fprintf( stderr, "...\n" );
		error = 1;
	}
	if ( file && strstr( file, "acodec-list" ) )
	{
		fprintf( stderr, "---\naudio_codecs:\n" );
		AVCodec *codec = NULL;
		while ( ( codec = av_codec_next( codec ) ) )
			if ( codec->decode && codec->type == CODEC_TYPE_AUDIO )
				fprintf( stderr, "  - %s\n", codec->name );
		fprintf( stderr, "...\n" );
		error = 1;
	}
	if ( file && strstr( file, "vcodec-list" ) )
	{
		fprintf( stderr, "---\nvideo_codecs:\n" );
		AVCodec *codec = NULL;
		while ( ( codec = av_codec_next( codec ) ) )
			if ( codec->decode && codec->type == CODEC_TYPE_VIDEO )
				fprintf( stderr, "  - %s\n", codec->name );
		fprintf( stderr, "...\n" );
		error = 1;
	}
	if ( error )
		return NULL;

	mlt_producer this = NULL;

	// Check that we have a non-NULL argument
	if ( file != NULL )
	{
		// Construct the producer
		this = calloc( 1, sizeof( struct mlt_producer_s ) );

		// Initialise it
		if ( mlt_producer_init( this, NULL ) == 0 )
		{
			// Get the properties
			mlt_properties properties = MLT_PRODUCER_PROPERTIES( this );

			// Set the resource property (required for all producers)
			mlt_properties_set( properties, "resource", file );

			// Register our get_frame implementation
			this->get_frame = producer_get_frame;

			// Open the file
			if ( producer_open( this, profile, file ) != 0 )
			{
				// Clean up
				mlt_producer_close( this );
				this = NULL;
			}
			else
			{
				// Close the file to release resources for large playlists - reopen later as needed
				mlt_properties_set_data( properties, "dummy_context", NULL, 0, NULL, NULL );
				mlt_properties_set_data( properties, "audio_context", NULL, 0, NULL, NULL );
				mlt_properties_set_data( properties, "video_context", NULL, 0, NULL, NULL );

				// Default the user-selectable indices from the auto-detected indices
				mlt_properties_set_int( properties, "audio_index",  mlt_properties_get_int( properties, "_audio_index" ) );
				mlt_properties_set_int( properties, "video_index",  mlt_properties_get_int( properties, "_video_index" ) );
			}
		}
	}

	return this;
}

/** Find the default streams.
*/

static mlt_properties find_default_streams( mlt_properties meta_media, AVFormatContext *context, int *audio_index, int *video_index )
{
	int i;
	char key[200];

	mlt_properties_set_int( meta_media, "meta.media.nb_streams", context->nb_streams );

	// Allow for multiple audio and video streams in the file and select first of each (if available)
	for( i = 0; i < context->nb_streams; i++ )
	{
		// Get the codec context
		AVStream *stream = context->streams[ i ];
		if ( ! stream ) continue;
		AVCodecContext *codec_context = stream->codec;
		if ( ! codec_context ) continue;
		AVCodec *codec = avcodec_find_decoder( codec_context->codec_id );
		if ( ! codec ) continue;

		snprintf( key, sizeof(key), "meta.media.%d.stream.type", i );

		// Determine the type and obtain the first index of each type
		switch( codec_context->codec_type )
		{
			case CODEC_TYPE_VIDEO:
				if ( *video_index < 0 )
					*video_index = i;
				mlt_properties_set( meta_media, key, "video" );
				snprintf( key, sizeof(key), "meta.media.%d.stream.frame_rate", i );
				mlt_properties_set_double( meta_media, key, av_q2d( context->streams[ i ]->r_frame_rate ) );
#if LIBAVFORMAT_VERSION_INT >= ((52<<16)+(21<<8)+0)
				snprintf( key, sizeof(key), "meta.media.%d.stream.sample_aspect_ratio", i );
				mlt_properties_set_double( meta_media, key, av_q2d( context->streams[ i ]->sample_aspect_ratio ) );
#endif
				snprintf( key, sizeof(key), "meta.media.%d.codec.pix_fmt", i );
				mlt_properties_set( meta_media, key, avcodec_get_pix_fmt_name( codec_context->pix_fmt ) );
				snprintf( key, sizeof(key), "meta.media.%d.codec.sample_aspect_ratio", i );
				mlt_properties_set_double( meta_media, key, av_q2d( codec_context->sample_aspect_ratio ) );
				break;
			case CODEC_TYPE_AUDIO:
				if ( *audio_index < 0 )
					*audio_index = i;
				mlt_properties_set( meta_media, key, "audio" );
#if (LIBAVCODEC_VERSION_INT >= ((51<<16)+(71<<8)+0))
				snprintf( key, sizeof(key), "meta.media.%d.codec.sample_fmt", i );
				mlt_properties_set( meta_media, key, avcodec_get_sample_fmt_name( codec_context->sample_fmt ) );
#endif
				snprintf( key, sizeof(key), "meta.media.%d.codec.sample_rate", i );
				mlt_properties_set_int( meta_media, key, codec_context->sample_rate );
				snprintf( key, sizeof(key), "meta.media.%d.codec.channels", i );
				mlt_properties_set_int( meta_media, key, codec_context->channels );
				break;
			default:
				break;
		}
// 		snprintf( key, sizeof(key), "meta.media.%d.stream.time_base", i );
// 		mlt_properties_set_double( meta_media, key, av_q2d( context->streams[ i ]->time_base ) );
		snprintf( key, sizeof(key), "meta.media.%d.codec.name", i );
		mlt_properties_set( meta_media, key, codec->name );
#if (LIBAVCODEC_VERSION_INT >= ((51<<16)+(55<<8)+0))
		snprintf( key, sizeof(key), "meta.media.%d.codec.long_name", i );
		mlt_properties_set( meta_media, key, codec->long_name );
#endif
		snprintf( key, sizeof(key), "meta.media.%d.codec.bit_rate", i );
		mlt_properties_set_int( meta_media, key, codec_context->bit_rate );
// 		snprintf( key, sizeof(key), "meta.media.%d.codec.time_base", i );
// 		mlt_properties_set_double( meta_media, key, av_q2d( codec_context->time_base ) );
		snprintf( key, sizeof(key), "meta.media.%d.codec.profile", i );
		mlt_properties_set_int( meta_media, key, codec_context->profile );
		snprintf( key, sizeof(key), "meta.media.%d.codec.level", i );
		mlt_properties_set_int( meta_media, key, codec_context->level );
	}

	return meta_media;
}

/** Producer file destructor.
*/

static void producer_file_close( void *context )
{
	if ( context != NULL )
	{
		// Lock the mutex now
		avformat_lock( );

		// Close the file
		av_close_input_file( context );

		// Unlock the mutex now
		avformat_unlock( );
	}
}

/** Producer file destructor.
*/

static void producer_codec_close( void *codec )
{
	if ( codec != NULL )
	{
		// Lock the mutex now
		avformat_lock( );

		// Close the file
		avcodec_close( codec );

		// Unlock the mutex now
		avformat_unlock( );
	}
}

static inline int dv_is_pal( AVPacket *pkt )
{
	return pkt->data[3] & 0x80;
}

static int dv_is_wide( AVPacket *pkt )
{
	int i = 80 /* block size */ *3 /* VAUX starts at block 3 */ +3 /* skip block header */;

	for ( ; i < pkt->size; i += 5 /* packet size */ )
	{
		if ( pkt->data[ i ] == 0x61 )
		{
			uint8_t x = pkt->data[ i + 2 ] & 0x7;
			return ( x == 2 ) || ( x == 7 );
		}
	}
	return 0;
}

static double get_aspect_ratio( AVStream *stream, AVCodecContext *codec_context, AVPacket *pkt )
{
	double aspect_ratio = 1.0;

	if ( codec_context->codec_id == CODEC_ID_DVVIDEO )
	{
		if ( pkt )
		{
			if ( dv_is_pal( pkt ) )
			{
				aspect_ratio = dv_is_wide( pkt )
					? 64.0/45.0 // 16:9 PAL
					: 16.0/15.0; // 4:3 PAL
			}
			else
			{
				aspect_ratio = dv_is_wide( pkt )
					? 32.0/27.0 // 16:9 NTSC
					: 8.0/9.0; // 4:3 NTSC
			}
		}
		else
		{
			AVRational ar =
#if LIBAVFORMAT_VERSION_INT >= ((52<<16)+(21<<8)+0)
				stream->sample_aspect_ratio;
#else
				codec_context->sample_aspect_ratio;
#endif
			// Override FFmpeg's notion of DV aspect ratios, which are
			// based upon a width of 704. Since we do not have a normaliser
			// that crops (nor is cropping 720 wide ITU-R 601 video always desirable)
			// we just coerce the values to facilitate a passive behaviour through
			// the rescale normaliser when using equivalent producers and consumers.
			// = display_aspect / (width * height)
			if ( ar.num == 10 && ar.den == 11 )
				aspect_ratio = 8.0/9.0; // 4:3 NTSC
			else if ( ar.num == 59 && ar.den == 54 )
				aspect_ratio = 16.0/15.0; // 4:3 PAL
			else if ( ar.num == 40 && ar.den == 33 )
				aspect_ratio = 32.0/27.0; // 16:9 NTSC
			else if ( ar.num == 118 && ar.den == 81 )
				aspect_ratio = 64.0/45.0; // 16:9 PAL
		}
	}
	else
	{
		AVRational codec_sar = codec_context->sample_aspect_ratio;
		AVRational stream_sar =
#if LIBAVFORMAT_VERSION_INT >= ((52<<16)+(21<<8)+0)
			stream->sample_aspect_ratio;
#else
			{ 0, 1 };
#endif
		if ( codec_sar.num > 0 )
			aspect_ratio = av_q2d( codec_sar );
		else if ( stream_sar.num > 0 )
			aspect_ratio = av_q2d( stream_sar );
	}
	return aspect_ratio;
}

/** Open the file.
*/

static int producer_open( mlt_producer this, mlt_profile profile, char *file )
{
	// Return an error code (0 == no error)
	int error = 0;

	// Context for avformat
	AVFormatContext *context = NULL;

	// Get the properties
	mlt_properties properties = MLT_PRODUCER_PROPERTIES( this );

	// We will treat everything with the producer fps
	double fps = mlt_profile_fps( profile );

	// Lock the mutex now
	avformat_lock( );

	// If "MRL", then create AVInputFormat
	AVInputFormat *format = NULL;
	AVFormatParameters *params = NULL;
	char *standard = NULL;
	char *mrl = strchr( file, ':' );

	// AV option (0 = both, 1 = video, 2 = audio)
	int av = 0;

	// Only if there is not a protocol specification that avformat can handle
	if ( mrl && !url_exist( file ) )
	{
		// 'file' becomes format abbreviation
		mrl[0] = 0;

		// Lookup the format
		format = av_find_input_format( file );

		// Eat the format designator
		file = ++mrl;

		if ( format )
		{
			// Allocate params
			params = calloc( sizeof( AVFormatParameters ), 1 );

			// These are required by video4linux (defaults)
			params->width = 640;
			params->height = 480;
			params->time_base= (AVRational){1,25};
			// params->device = file;
			params->channels = 2;
			params->sample_rate = 48000;
		}

		// XXX: this does not work anymore since avdevice
		// TODO: make producer_avddevice?
		// Parse out params
		mrl = strchr( file, '?' );
		while ( mrl )
		{
			mrl[0] = 0;
			char *name = strdup( ++mrl );
			char *value = strchr( name, ':' );
			if ( value )
			{
				value[0] = 0;
				value++;
				char *t = strchr( value, '&' );
				if ( t )
					t[0] = 0;
				if ( !strcmp( name, "frame_rate" ) )
					params->time_base.den = atoi( value );
				else if ( !strcmp( name, "frame_rate_base" ) )
					params->time_base.num = atoi( value );
				else if ( !strcmp( name, "sample_rate" ) )
					params->sample_rate = atoi( value );
				else if ( !strcmp( name, "channels" ) )
					params->channels = atoi( value );
				else if ( !strcmp( name, "width" ) )
					params->width = atoi( value );
				else if ( !strcmp( name, "height" ) )
					params->height = atoi( value );
				else if ( !strcmp( name, "standard" ) )
				{
					standard = strdup( value );
					params->standard = standard;
				}
				else if ( !strcmp( name, "av" ) )
					av = atoi( value );
			}
			free( name );
			mrl = strchr( mrl, '&' );
		}
	}

	// Now attempt to open the file
	error = av_open_input_file( &context, file, format, 0, params ) < 0;

	// Cleanup AVFormatParameters
	free( standard );
	free( params );

	// If successful, then try to get additional info
	if ( error == 0 )
	{
		// Get the stream info
		error = av_find_stream_info( context ) < 0;

		// Continue if no error
		if ( error == 0 )
		{
			// We will default to the first audio and video streams found
			int audio_index = -1;
			int video_index = -1;
			int av_bypass = 0;

			// Now set properties where we can (use default unknowns if required)
			if ( context->duration != AV_NOPTS_VALUE )
			{
				// This isn't going to be accurate for all formats
				mlt_position frames = ( mlt_position )( ( ( double )context->duration / ( double )AV_TIME_BASE ) * fps + 0.5 );
				mlt_properties_set_position( properties, "out", frames - 1 );
				mlt_properties_set_position( properties, "length", frames );
			}

			// Find default audio and video streams
			find_default_streams( properties, context, &audio_index, &video_index );

			if ( context->start_time != AV_NOPTS_VALUE )
				mlt_properties_set_double( properties, "_start_time", context->start_time );

			// Check if we're seekable (something funny about mpeg here :-/)
			if ( strcmp( file, "pipe:" ) && strncmp( file, "http://", 6 )  && strncmp( file, "udp:", 4 )  && strncmp( file, "tcp:", 4 ) && strncmp( file, "rtsp:", 5 )  && strncmp( file, "rtp:", 4 ) )
			{
				mlt_properties_set_int( properties, "seekable", av_seek_frame( context, -1, mlt_properties_get_double( properties, "_start_time" ), AVSEEK_FLAG_BACKWARD ) >= 0 );
				mlt_properties_set_data( properties, "dummy_context", context, 0, producer_file_close, NULL );
				av_open_input_file( &context, file, NULL, 0, NULL );
				av_find_stream_info( context );
			}
			else
				av_bypass = 1;

			// Store selected audio and video indexes on properties
			mlt_properties_set_int( properties, "_audio_index", audio_index );
			mlt_properties_set_int( properties, "_video_index", video_index );
			mlt_properties_set_int( properties, "_first_pts", -1 );
			mlt_properties_set_int( properties, "_last_position", POSITION_INITIAL );

			// Fetch the width, height and aspect ratio
			if ( video_index != -1 )
			{
				AVCodecContext *codec_context = context->streams[ video_index ]->codec;
				mlt_properties_set_int( properties, "width", codec_context->width );
				mlt_properties_set_int( properties, "height", codec_context->height );

				if ( codec_context->codec_id == CODEC_ID_DVVIDEO )
				{
					// Fetch the first frame of DV so we can read it directly
					AVPacket pkt;
					int ret = 0;
					while ( ret >= 0 )
					{
						ret = av_read_frame( context, &pkt );
						if ( ret >= 0 && pkt.stream_index == video_index && pkt.size > 0 )
						{
							mlt_properties_set_double( properties, "aspect_ratio",
								get_aspect_ratio( context->streams[ video_index ], codec_context, &pkt ) );
							break;
						}
					}
				}
				else
				{
					mlt_properties_set_double( properties, "aspect_ratio",
						get_aspect_ratio( context->streams[ video_index ], codec_context, NULL ) );
				}
			}

			// Read Metadata
			if (context->title != NULL)
				mlt_properties_set(properties, "meta.attr.title.markup", context->title );
			if (context->author != NULL)
				mlt_properties_set(properties, "meta.attr.author.markup", context->author );
			if (context->copyright != NULL)
				mlt_properties_set(properties, "meta.attr.copyright.markup", context->copyright );
			if (context->comment != NULL)
				mlt_properties_set(properties, "meta.attr.comment.markup", context->comment );
			if (context->album != NULL)
				mlt_properties_set(properties, "meta.attr.album.markup", context->album );
			if (context->year != 0)
				mlt_properties_set_int(properties, "meta.attr.year.markup", context->year );
			if (context->track != 0)
				mlt_properties_set_int(properties, "meta.attr.track.markup", context->track );

			// We're going to cheat here - for a/v files, we will have two contexts (reasoning will be clear later)
			if ( av == 0 && audio_index != -1 && video_index != -1 )
			{
				// We'll use the open one as our video_context
				mlt_properties_set_data( properties, "video_context", context, 0, producer_file_close, NULL );

				// And open again for our audio context
				av_open_input_file( &context, file, NULL, 0, NULL );
				av_find_stream_info( context );

				// Audio context
				mlt_properties_set_data( properties, "audio_context", context, 0, producer_file_close, NULL );
			}
			else if ( av != 2 && video_index != -1 )
			{
				// We only have a video context
				mlt_properties_set_data( properties, "video_context", context, 0, producer_file_close, NULL );
			}
			else if ( audio_index != -1 )
			{
				// We only have an audio context
				mlt_properties_set_data( properties, "audio_context", context, 0, producer_file_close, NULL );
			}
			else
			{
				// Something has gone wrong
				error = -1;
			}

			mlt_properties_set_int( properties, "av_bypass", av_bypass );
		}
	}

	// Unlock the mutex now
	avformat_unlock( );

	return error;
}

/** Convert a frame position to a time code.
*/

static double producer_time_of_frame( mlt_producer this, mlt_position position )
{
	return ( double )position / mlt_producer_get_fps( this );
}

static inline void convert_image( AVFrame *frame, uint8_t *buffer, int pix_fmt, mlt_image_format *format, int width, int height )
{
#ifdef SWSCALE
	if ( pix_fmt == PIX_FMT_RGB32 )
	{
		*format = mlt_image_rgb24a;
		struct SwsContext *context = sws_getContext( width, height, pix_fmt,
			width, height, PIX_FMT_RGBA, SWS_FAST_BILINEAR, NULL, NULL, NULL);
		AVPicture output;
		avpicture_fill( &output, buffer, PIX_FMT_RGBA, width, height );
		sws_scale( context, frame->data, frame->linesize, 0, height,
			output.data, output.linesize);
		sws_freeContext( context );
	}
	else if ( *format == mlt_image_yuv420p )
	{
		struct SwsContext *context = sws_getContext( width, height, pix_fmt,
			width, height, PIX_FMT_YUV420P, SWS_FAST_BILINEAR, NULL, NULL, NULL);
		AVPicture output;
		output.data[0] = buffer;
		output.data[1] = buffer + width * height;
		output.data[2] = buffer + ( 3 * width * height ) / 2;
		output.linesize[0] = width;
		output.linesize[1] = width >> 1;
		output.linesize[2] = width >> 1;
		sws_scale( context, frame->data, frame->linesize, 0, height,
			output.data, output.linesize);
		sws_freeContext( context );
	}
	else if ( *format == mlt_image_rgb24 )
	{
		struct SwsContext *context = sws_getContext( width, height, pix_fmt,
			width, height, PIX_FMT_RGB24, SWS_FAST_BILINEAR, NULL, NULL, NULL);
		AVPicture output;
		avpicture_fill( &output, buffer, PIX_FMT_RGB24, width, height );
		sws_scale( context, frame->data, frame->linesize, 0, height,
			output.data, output.linesize);
		sws_freeContext( context );
	}
	else if ( *format == mlt_image_rgb24a || *format == mlt_image_opengl )
	{
		struct SwsContext *context = sws_getContext( width, height, pix_fmt,
			width, height, PIX_FMT_RGBA, SWS_FAST_BILINEAR, NULL, NULL, NULL);
		AVPicture output;
		avpicture_fill( &output, buffer, PIX_FMT_RGBA, width, height );
		sws_scale( context, frame->data, frame->linesize, 0, height,
			output.data, output.linesize);
		sws_freeContext( context );
	}
	else
	{
		struct SwsContext *context = sws_getContext( width, height, pix_fmt,
			width, height, PIX_FMT_YUYV422, SWS_FAST_BILINEAR, NULL, NULL, NULL);
		AVPicture output;
		avpicture_fill( &output, buffer, PIX_FMT_YUYV422, width, height );
		sws_scale( context, frame->data, frame->linesize, 0, height,
			output.data, output.linesize);
		sws_freeContext( context );
	}
#else
	if ( *format == mlt_image_yuv420p )
	{
		AVPicture pict;
		pict.data[0] = buffer;
		pict.data[1] = buffer + width * height;
		pict.data[2] = buffer + ( 3 * width * height ) / 2;
		pict.linesize[0] = width;
		pict.linesize[1] = width >> 1;
		pict.linesize[2] = width >> 1;
		img_convert( &pict, PIX_FMT_YUV420P, (AVPicture *)frame, pix_fmt, width, height );
	}
	else if ( *format == mlt_image_rgb24 )
	{
		AVPicture output;
		avpicture_fill( &output, buffer, PIX_FMT_RGB24, width, height );
		img_convert( &output, PIX_FMT_RGB24, (AVPicture *)frame, pix_fmt, width, height );
	}
	else if ( format == mlt_image_rgb24a || format == mlt_image_opengl )
	{
		AVPicture output;
		avpicture_fill( &output, buffer, PIX_FMT_RGB32, width, height );
		img_convert( &output, PIX_FMT_RGB32, (AVPicture *)frame, pix_fmt, width, height );
	}
	else
	{
		AVPicture output;
		avpicture_fill( &output, buffer, PIX_FMT_YUYV422, width, height );
		img_convert( &output, PIX_FMT_YUYV422, (AVPicture *)frame, pix_fmt, width, height );
	}
#endif
}

/** Allocate the image buffer and set it on the frame.
*/

static int allocate_buffer( mlt_properties frame_properties, AVCodecContext *codec_context, uint8_t **buffer, mlt_image_format *format, int *width, int *height )
{
	int size = 0;

	if ( codec_context->width == 0 || codec_context->height == 0 )
		return size;

	*width = codec_context->width;
	*height = codec_context->height;
	mlt_properties_set_int( frame_properties, "width", *width );
	mlt_properties_set_int( frame_properties, "height", *height );

	if ( codec_context->pix_fmt == PIX_FMT_RGB32 )
		size = *width * ( *height + 1 ) * 4;
	else switch ( *format )
	{
		case mlt_image_yuv420p:
			size = *width * 3 * ( *height + 1 ) / 2;
			break;
		case mlt_image_rgb24:
			size = *width * ( *height + 1 ) * 3;
			break;
		case mlt_image_rgb24a:
		case mlt_image_opengl:
			size = *width * ( *height + 1 ) * 4;
			break;
		default:
			*format = mlt_image_yuv422;
			size = *width * ( *height + 1 ) * 2;
			break;
	}

	// Construct the output image
	*buffer = mlt_pool_alloc( size );
	if ( *buffer )
		mlt_properties_set_data( frame_properties, "image", *buffer, size, mlt_pool_release, NULL );
	else
		size = 0;

	return size;
}

/** Get an image from a frame.
*/

static int producer_get_image( mlt_frame frame, uint8_t **buffer, mlt_image_format *format, int *width, int *height, int writable )
{
	// Get the properties from the frame
	mlt_properties frame_properties = MLT_FRAME_PROPERTIES( frame );

	// Obtain the frame number of this frame
	mlt_position position = mlt_properties_get_position( frame_properties, "avformat_position" );

	// Get the producer
	mlt_producer this = mlt_properties_get_data( frame_properties, "avformat_producer", NULL );

	// Get the producer properties
	mlt_properties properties = MLT_PRODUCER_PROPERTIES( this );

	avformat_lock();

	// Fetch the video_context
	AVFormatContext *context = mlt_properties_get_data( properties, "video_context", NULL );

	// Get the video_index
	int index = mlt_properties_get_int( properties, "video_index" );

	// Obtain the expected frame numer
	mlt_position expected = mlt_properties_get_position( properties, "_video_expected" );

	// Get the video stream
	AVStream *stream = context->streams[ index ];

	// Get codec context
	AVCodecContext *codec_context = stream->codec;

	// Packet
	AVPacket pkt;

	// Get the conversion frame
	AVFrame *av_frame = mlt_properties_get_data( properties, "av_frame", NULL );

	// Special case pause handling flag
	int paused = 0;

	// Special case ffwd handling
	int ignore = 0;

	// We may want to use the source fps if available
	double source_fps = mlt_properties_get_double( properties, "source_fps" );
	double fps = mlt_producer_get_fps( this );

	// Get pts info
	int first_pts = mlt_properties_get_int( properties, "_first_pts" );

	// This is the physical frame position in the source
	int req_position = ( int )( position / fps * source_fps + 0.5 );

	// Get the seekable status
	int seekable = mlt_properties_get_int( properties, "seekable" );

	// Hopefully provide better support for streams...
	int av_bypass = mlt_properties_get_int( properties, "av_bypass" );

	// Determines if we have to decode all frames in a sequence
	int must_decode = 1;

	// Temporary hack to improve intra frame only
	must_decode = strcmp( codec_context->codec->name, "dnxhd" ) &&
				  strcmp( codec_context->codec->name, "dvvideo" ) &&
				  strcmp( codec_context->codec->name, "huffyuv" ) &&
				  strcmp( codec_context->codec->name, "mjpeg" ) &&
				  strcmp( codec_context->codec->name, "rawvideo" );

	int last_position = mlt_properties_get_int( properties, "_last_position" );

	// Turn on usage of new seek API and PTS for seeking
	int use_new_seek = codec_context->codec_id == CODEC_ID_H264 && !strcmp( context->iformat->name, "mpegts" );
	if ( mlt_properties_get( properties, "new_seek" ) )
		use_new_seek = mlt_properties_get_int( properties, "new_seek" );

	// Seek if necessary
	if ( position != expected || last_position < 0 )
	{
		if ( av_frame != NULL && position + 1 == expected )
		{
			// We're paused - use last image
			paused = 1;
		}
		else if ( !seekable && position > expected && ( position - expected ) < 250 )
		{
			// Fast forward - seeking is inefficient for small distances - just ignore following frames
			ignore = ( int )( ( position - expected ) / fps * source_fps );
			codec_context->skip_loop_filter = AVDISCARD_NONREF;
		}
		else if ( seekable && ( position < expected || position - expected >= 12 || last_position < 0 ) )
		{
			if ( use_new_seek && last_position == POSITION_INITIAL )
			{
				// find first key frame
				int ret = 0;
				int toscan = 100;

				while ( ret >= 0 && toscan-- > 0 )
				{
					ret = av_read_frame( context, &pkt );
					if ( ret >= 0 && ( pkt.flags & PKT_FLAG_KEY ) && pkt.stream_index == index )
					{
						mlt_log_verbose( MLT_PRODUCER_SERVICE(this), "first_pts %lld dts %lld pts_dts_delta %d\n", pkt.pts, pkt.dts, (int)(pkt.pts - pkt.dts) );
						first_pts = pkt.pts;
						mlt_properties_set_int( properties, "_first_pts", first_pts );
						toscan = 0;
					}
					av_free_packet( &pkt );
				}
				// Rewind
				av_seek_frame( context, -1, 0, AVSEEK_FLAG_BACKWARD );
			}

			// Calculate the timestamp for the requested frame
			int64_t timestamp;
			if ( use_new_seek )
			{
				timestamp = ( req_position - 0.1 / source_fps ) /
					( av_q2d( stream->time_base ) * source_fps );
				mlt_log_verbose( MLT_PRODUCER_SERVICE(this), "pos %d pts %lld ", req_position, timestamp );
				if ( first_pts > 0 )
					timestamp += first_pts;
				else if ( context->start_time != AV_NOPTS_VALUE )
					timestamp += context->start_time;
			}
			else
			{
				timestamp = ( int64_t )( ( double )req_position / source_fps * AV_TIME_BASE + 0.5 );
				if ( context->start_time != AV_NOPTS_VALUE )
					timestamp += context->start_time;
			}
			if ( must_decode )
				timestamp -= AV_TIME_BASE;
			if ( timestamp < 0 )
				timestamp = 0;
			mlt_log_debug( MLT_PRODUCER_SERVICE( this ), "seeking timestamp %lld position %d expected %d last_pos %d\n",
				timestamp, position, expected, last_position );

			// Seek to the timestamp
			if ( use_new_seek )
			{
				codec_context->skip_loop_filter = AVDISCARD_NONREF;
				av_seek_frame( context, index, timestamp, AVSEEK_FLAG_BACKWARD );
			}
			else
			{
				av_seek_frame( context, -1, timestamp, AVSEEK_FLAG_BACKWARD );
			}

			// Remove the cached info relating to the previous position
			mlt_properties_set_int( properties, "_current_position", -1 );
			mlt_properties_set_int( properties, "_last_position", POSITION_INVALID );
			mlt_properties_set_data( properties, "av_frame", NULL, 0, NULL, NULL );
			av_frame = NULL;

			if ( use_new_seek )
			{
				// flush any pictures still in decode buffer
				avcodec_flush_buffers( codec_context );
			}
		}
	}

	// Duplicate the last image if necessary (see comment on rawvideo below)
	int current_position = mlt_properties_get_int( properties, "_current_position" );
	int got_picture = mlt_properties_get_int( properties, "_got_picture" );
	if ( av_frame != NULL && got_picture && ( paused || current_position == req_position || ( !use_new_seek && current_position > req_position ) ) && av_bypass == 0 )
	{
		// Duplicate it
		if ( allocate_buffer( frame_properties, codec_context, buffer, format, width, height ) )
			convert_image( av_frame, *buffer, codec_context->pix_fmt, format, *width, *height );
		else
			mlt_frame_get_image( frame, buffer, format, width, height, writable );
	}
	else
	{
		int ret = 0;
		int int_position = 0;
		int decode_errors = 0;
		got_picture = 0;

		av_init_packet( &pkt );

		// Construct an AVFrame for YUV422 conversion
		if ( av_frame == NULL )
			av_frame = avcodec_alloc_frame( );

		while( ret >= 0 && !got_picture )
		{
			// Read a packet
			ret = av_read_frame( context, &pkt );

			// We only deal with video from the selected video_index
			if ( ret >= 0 && pkt.stream_index == index && pkt.size > 0 )
			{
				// Determine time code of the packet
				if ( use_new_seek )
				{
					int64_t pts = pkt.pts;
					if ( first_pts > 0 )
						pts -= first_pts;
					else if ( context->start_time != AV_NOPTS_VALUE )
						pts -= context->start_time;
					int_position = ( int )( av_q2d( stream->time_base ) * pts * source_fps + 0.1 );
				}
				else
				{
					if ( pkt.dts != AV_NOPTS_VALUE )
					{
						int_position = ( int )( av_q2d( stream->time_base ) * pkt.dts * source_fps + 0.5 );
						if ( context->start_time != AV_NOPTS_VALUE )
							int_position -= ( int )( context->start_time * source_fps / AV_TIME_BASE + 0.5 );
						last_position = mlt_properties_get_int( properties, "_last_position" );
						if ( int_position == last_position )
							int_position = last_position + 1;
					}
					else
					{
						int_position = req_position;
					}
					mlt_log_debug( MLT_PRODUCER_SERVICE(this), "pkt.dts %llu req_pos %d cur_pos %d pkt_pos %d",
						pkt.dts, req_position, current_position, int_position );
					// Make a dumb assumption on streams that contain wild timestamps
					if ( abs( req_position - int_position ) > 999 )
					{
						int_position = req_position;
						mlt_log_debug( MLT_PRODUCER_SERVICE(this), " WILD TIMESTAMP!" );
					}
				}
				mlt_properties_set_int( properties, "_last_position", int_position );

				// Decode the image
				if ( must_decode || int_position >= req_position )
				{
					codec_context->reordered_opaque = pkt.pts;
					if ( int_position >= req_position )
						codec_context->skip_loop_filter = AVDISCARD_NONE;
					ret = avcodec_decode_video( codec_context, av_frame, &got_picture, pkt.data, pkt.size );
					// Note: decode may fail at the beginning of MPEGfile (B-frames referencing before first I-frame), so allow a few errors.
					if ( ret < 0 )
					{
						if ( ++decode_errors <= 10 )
							ret = 0;
					}
					else
					{
						decode_errors = 0;
					}
				}

				if ( got_picture )
				{
					if ( use_new_seek )
					{
						// Determine time code of the packet
						int64_t pts = av_frame->reordered_opaque;
						if ( first_pts > 0 )
							pts -= first_pts;
						else if ( context->start_time != AV_NOPTS_VALUE )
							pts -= context->start_time;
						int_position = ( int )( av_q2d( stream->time_base) * pts * source_fps + 0.1 );
						mlt_log_verbose( MLT_PRODUCER_SERVICE(this), "got frame %d, key %d\n", int_position, av_frame->key_frame );
					}
					// Handle ignore
					if ( int_position < req_position )
					{
						ignore = 0;
						got_picture = 0;
					}
					else if ( int_position >= req_position )
					{
						ignore = 0;
						codec_context->skip_loop_filter = AVDISCARD_NONE;
					}
					else if ( ignore -- )
					{
						got_picture = 0;
					}
				}
				mlt_log_debug( MLT_PRODUCER_SERVICE(this), " got_pic %d key %d\n", got_picture, pkt.flags & PKT_FLAG_KEY );
				av_free_packet( &pkt );
			}
			else if ( ret >= 0 )
			{
				av_free_packet( &pkt );
			}

			// Now handle the picture if we have one
			if ( got_picture )
			{
				if ( allocate_buffer( frame_properties, codec_context, buffer, format, width, height ) )
				{
					convert_image( av_frame, *buffer, codec_context->pix_fmt, format, *width, *height );
					if ( !mlt_properties_get( properties, "force_progressive" ) )
						mlt_properties_set_int( frame_properties, "progressive", !av_frame->interlaced_frame );
					mlt_properties_set_int( properties, "top_field_first", av_frame->top_field_first );
					mlt_properties_set_int( properties, "_current_position", int_position );
					mlt_properties_set_int( properties, "_got_picture", 1 );
					mlt_properties_set_data( properties, "av_frame", av_frame, 0, av_free, NULL );
				}
				else
				{
					got_picture = 0;
				}
			}
		}
		if ( !got_picture )
			mlt_frame_get_image( frame, buffer, format, width, height, writable );
	}

	// Very untidy - for rawvideo, the packet contains the frame, hence the free packet
	// above will break the pause behaviour - so we wipe the frame now
	if ( !strcmp( codec_context->codec->name, "rawvideo" ) )
		mlt_properties_set_data( properties, "av_frame", NULL, 0, NULL, NULL );

	avformat_unlock();

	// Set the field order property for this frame
	mlt_properties_set_int( frame_properties, "top_field_first", mlt_properties_get_int( properties, "top_field_first" ) );

	// Regardless of speed, we expect to get the next frame (cos we ain't too bright)
	mlt_properties_set_position( properties, "_video_expected", position + 1 );

	return 0;
}

/** Process properties as AVOptions and apply to AV context obj
*/

static void apply_properties( void *obj, mlt_properties properties, int flags )
{
	int i;
	int count = mlt_properties_count( properties );
	for ( i = 0; i < count; i++ )
	{
		const char *opt_name = mlt_properties_get_name( properties, i );
		const AVOption *opt = av_find_opt( obj, opt_name, NULL, flags, flags );
		if ( opt != NULL )
#if LIBAVCODEC_VERSION_INT >= ((52<<16)+(7<<8)+0)
			av_set_string3( obj, opt_name, mlt_properties_get( properties, opt_name), 0, NULL );
#elif LIBAVCODEC_VERSION_INT >= ((51<<16)+(59<<8)+0)
			av_set_string2( obj, opt_name, mlt_properties_get( properties, opt_name), 0 );
#else
			av_set_string( obj, opt_name, mlt_properties_get( properties, opt_name) );
#endif
	}
}

/** Set up video handling.
*/

static void producer_set_up_video( mlt_producer this, mlt_frame frame )
{
	// Get the properties
	mlt_properties properties = MLT_PRODUCER_PROPERTIES( this );

	// Fetch the video_context
	AVFormatContext *context = mlt_properties_get_data( properties, "video_context", NULL );

	// Get the video_index
	int index = mlt_properties_get_int( properties, "video_index" );

	// Reopen the file if necessary
	if ( !context && index > -1 )
	{
		mlt_events_block( properties, this );
		producer_open( this, mlt_service_profile( MLT_PRODUCER_SERVICE(this) ),
			mlt_properties_get( properties, "resource" ) );
		context = mlt_properties_get_data( properties, "video_context", NULL );
		mlt_properties_set_data( properties, "dummy_context", NULL, 0, NULL, NULL );
		mlt_events_unblock( properties, this );

		// Process properties as AVOptions
		apply_properties( context, properties, AV_OPT_FLAG_DECODING_PARAM );
	}

	// Exception handling for video_index
	if ( context && index >= (int) context->nb_streams )
	{
		// Get the last video stream
		for ( index = context->nb_streams - 1; index >= 0 && context->streams[ index ]->codec->codec_type != CODEC_TYPE_VIDEO; --index );
		mlt_properties_set_int( properties, "video_index", index );
	}
	if ( context && index > -1 && context->streams[ index ]->codec->codec_type != CODEC_TYPE_VIDEO )
	{
		// Invalidate the video stream
		index = -1;
		mlt_properties_set_int( properties, "video_index", index );
	}

	// Get the frame properties
	mlt_properties frame_properties = MLT_FRAME_PROPERTIES( frame );

	if ( context && index > -1 )
	{
		// Get the video stream
		AVStream *stream = context->streams[ index ];

		// Get codec context
		AVCodecContext *codec_context = stream->codec;

		// Get the codec
		AVCodec *codec = mlt_properties_get_data( properties, "video_codec", NULL );

		// Update the video properties if the index changed
		if ( index != mlt_properties_get_int( properties, "_video_index" ) )
		{
			// Reset the video properties if the index changed
			mlt_properties_set_int( properties, "_video_index", index );
			mlt_properties_set_data( properties, "video_codec", NULL, 0, NULL, NULL );
			mlt_properties_set_int( properties, "width", codec_context->width );
			mlt_properties_set_int( properties, "height", codec_context->height );
			// TODO: get the first usable AVPacket and reset the stream position
			mlt_properties_set_double( properties, "aspect_ratio",
				get_aspect_ratio( context->streams[ index ], codec_context, NULL ) );
			codec = NULL;
		}

		// Initialise the codec if necessary
		if ( codec == NULL )
		{
			// Initialise multi-threading
			int thread_count = mlt_properties_get_int( properties, "threads" );
			if ( thread_count == 0 && getenv( "MLT_AVFORMAT_THREADS" ) )
				thread_count = atoi( getenv( "MLT_AVFORMAT_THREADS" ) );
			if ( thread_count > 1 )
			{
				avcodec_thread_init( codec_context, thread_count );
				codec_context->thread_count = thread_count;
			}

			// Find the codec
			codec = avcodec_find_decoder( codec_context->codec_id );

			// If we don't have a codec and we can't initialise it, we can't do much more...
			avformat_lock( );
			if ( codec != NULL && avcodec_open( codec_context, codec ) >= 0 )
			{
				// Now store the codec with its destructor
				mlt_properties_set_data( properties, "video_codec", codec_context, 0, producer_codec_close, NULL );
			}
			else
			{
				// Remember that we can't use this later
				mlt_properties_set_int( properties, "video_index", -1 );
				index = -1;
			}
			avformat_unlock( );

			// Process properties as AVOptions
			apply_properties( codec_context, properties, AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM );
		}

		// No codec, no show...
		if ( codec && index > -1 )
		{
			double source_fps = 0;
			double force_aspect_ratio = mlt_properties_get_double( properties, "force_aspect_ratio" );
			double aspect_ratio = ( force_aspect_ratio > 0.0 ) ?
				force_aspect_ratio : mlt_properties_get_double( properties, "aspect_ratio" );

			// Determine the fps
			source_fps = ( double )codec_context->time_base.den / ( codec_context->time_base.num == 0 ? 1 : codec_context->time_base.num );

			// If the muxer reports a frame rate different than the codec
			double muxer_fps = av_q2d( context->streams[ index ]->r_frame_rate );
			if ( source_fps != muxer_fps )
				// Choose the lesser - the wrong tends to be off by some multiple of 10
				source_fps = muxer_fps < source_fps ? muxer_fps : source_fps;

			// We'll use fps if it's available
			if ( source_fps > 0 )
				mlt_properties_set_double( properties, "source_fps", source_fps );
			else
				mlt_properties_set_double( properties, "source_fps", mlt_producer_get_fps( this ) );
			mlt_properties_set_double( properties, "aspect_ratio", aspect_ratio );

			// Set the width and height
			mlt_properties_set_int( frame_properties, "width", codec_context->width );
			mlt_properties_set_int( frame_properties, "height", codec_context->height );
			mlt_properties_set_int( frame_properties, "real_width", codec_context->width );
			mlt_properties_set_int( frame_properties, "real_height", codec_context->height );
			mlt_properties_set_double( frame_properties, "aspect_ratio", aspect_ratio );
			if ( mlt_properties_get( properties, "force_progressive" ) )
				mlt_properties_set_int( frame_properties, "progressive", mlt_properties_get_int( properties, "force_progressive" ) );

			mlt_frame_push_get_image( frame, producer_get_image );
			mlt_properties_set_data( frame_properties, "avformat_producer", this, 0, NULL, NULL );
		}
		else
		{
			mlt_properties_set_int( frame_properties, "test_image", 1 );
		}
	}
	else
	{
		mlt_properties_set_int( frame_properties, "test_image", 1 );
	}
}

/** Get the audio from a frame.
*/

static int producer_get_audio( mlt_frame frame, void **buffer, mlt_audio_format *format, int *frequency, int *channels, int *samples )
{
	// Get the properties from the frame
	mlt_properties frame_properties = MLT_FRAME_PROPERTIES( frame );

	// Obtain the frame number of this frame
	mlt_position position = mlt_properties_get_position( frame_properties, "avformat_position" );

	// Get the producer
	mlt_producer this = mlt_properties_get_data( frame_properties, "avformat_producer", NULL );

	// Get the producer properties
	mlt_properties properties = MLT_PRODUCER_PROPERTIES( this );

	// Fetch the audio_context
	AVFormatContext *context = mlt_properties_get_data( properties, "audio_context", NULL );

	// Get the audio_index
	int index = mlt_properties_get_int( properties, "audio_index" );

	// Get the seekable status
	int seekable = mlt_properties_get_int( properties, "seekable" );

	// Obtain the expected frame numer
	mlt_position expected = mlt_properties_get_position( properties, "_audio_expected" );

	// Obtain the resample context if it exists (not always needed)
	ReSampleContext *resample = mlt_properties_get_data( properties, "audio_resample", NULL );

	// Obtain the audio buffers
	int16_t *audio_buffer = mlt_properties_get_data( properties, "audio_buffer", NULL );
	int16_t *decode_buffer = mlt_properties_get_data( properties, "decode_buffer", NULL );

	// Get amount of audio used
	int audio_used =  mlt_properties_get_int( properties, "_audio_used" );

	// Calculate the real time code
	double real_timecode = producer_time_of_frame( this, position );

	// Get the audio stream
	AVStream *stream = context->streams[ index ];

	// Get codec context
	AVCodecContext *codec_context = stream->codec;

	// Packet
	AVPacket pkt;

	// Number of frames to ignore (for ffwd)
	int ignore = 0;

	// Flag for paused (silence)
	int paused = 0;

	// Check for resample and create if necessary
	if ( resample == NULL && codec_context->channels <= 2 )
	{
		// Create the resampler
#if (LIBAVCODEC_VERSION_INT >= ((52<<16)+(15<<8)+0))
		resample = av_audio_resample_init( *channels, codec_context->channels, *frequency, codec_context->sample_rate,
			SAMPLE_FMT_S16, codec_context->sample_fmt, 16, 10, 0, 0.8 );
#else
		resample = audio_resample_init( *channels, codec_context->channels, *frequency, codec_context->sample_rate );
#endif

		// And store it on properties
		mlt_properties_set_data( properties, "audio_resample", resample, 0, ( mlt_destructor )audio_resample_close, NULL );
	}
	else if ( resample == NULL )
	{
		// TODO: uncomment and remove following line when full multi-channel support is ready
		// *channels = codec_context->channels;
		codec_context->request_channels = *channels;

		*frequency = codec_context->sample_rate;
	}

	// Check for audio buffer and create if necessary
	if ( audio_buffer == NULL )
	{
		// Allocate the audio buffer
		audio_buffer = mlt_pool_alloc( AVCODEC_MAX_AUDIO_FRAME_SIZE * sizeof( int16_t ) );

		// And store it on properties for reuse
		mlt_properties_set_data( properties, "audio_buffer", audio_buffer, 0, ( mlt_destructor )mlt_pool_release, NULL );
	}

	// Check for decoder buffer and create if necessary
	if ( decode_buffer == NULL )
	{
		// Allocate the audio buffer
		decode_buffer = av_malloc( AVCODEC_MAX_AUDIO_FRAME_SIZE * sizeof( int16_t ) );

		// And store it on properties for reuse
		mlt_properties_set_data( properties, "decode_buffer", decode_buffer, 0, av_free, NULL );
	}

	// Seek if necessary
	if ( position != expected )
	{
		if ( position + 1 == expected )
		{
			// We're paused - silence required
			paused = 1;
		}
		else if ( !seekable && position > expected && ( position - expected ) < 250 )
		{
			// Fast forward - seeking is inefficient for small distances - just ignore following frames
			ignore = position - expected;
		}
		else if ( position < expected || position - expected >= 12 )
		{
			int64_t timestamp = ( int64_t )( real_timecode * AV_TIME_BASE + 0.5 );
			if ( context->start_time != AV_NOPTS_VALUE )
				timestamp += context->start_time;
			if ( timestamp < 0 )
				timestamp = 0;

			// Set to the real timecode
			if ( av_seek_frame( context, -1, timestamp, AVSEEK_FLAG_BACKWARD ) != 0 )
				paused = 1;

			// Clear the usage in the audio buffer
			audio_used = 0;
		}
	}

	// Get the audio if required
	if ( !paused )
	{
		int ret = 0;
		int got_audio = 0;

		av_init_packet( &pkt );

		while( ret >= 0 && !got_audio )
		{
			// Check if the buffer already contains the samples required
			if ( audio_used >= *samples && ignore == 0 )
			{
				got_audio = 1;
				break;
			}

			// Read a packet
			ret = av_read_frame( context, &pkt );

			int len = pkt.size;
			uint8_t *ptr = pkt.data;

			// We only deal with audio from the selected audio_index
			while ( ptr != NULL && ret >= 0 && pkt.stream_index == index && len > 0 )
			{
				int data_size = sizeof( int16_t ) * AVCODEC_MAX_AUDIO_FRAME_SIZE;

				// Decode the audio
#if (LIBAVCODEC_VERSION_INT >= ((51<<16)+(29<<8)+0))
				ret = avcodec_decode_audio2( codec_context, decode_buffer, &data_size, ptr, len );
#else
				ret = avcodec_decode_audio( codec_context, decode_buffer, &data_size, ptr, len );
#endif
				if ( ret < 0 )
				{
					ret = 0;
					break;
				}

				len -= ret;
				ptr += ret;

				if ( data_size > 0 && ( audio_used * *channels + data_size < AVCODEC_MAX_AUDIO_FRAME_SIZE ) )
				{
					if ( resample )
					{
						int16_t *source = decode_buffer;
						int16_t *dest = &audio_buffer[ audio_used * *channels ];
						int convert_samples = data_size / av_get_bits_per_sample_format( codec_context->sample_fmt ) * 8 / codec_context->channels;

						audio_used += audio_resample( resample, dest, source, convert_samples );
					}
					else
					{
						memcpy( &audio_buffer[ audio_used * *channels ], decode_buffer, data_size );
						audio_used += data_size / *channels / av_get_bits_per_sample_format( codec_context->sample_fmt ) * 8;
					}

					// Handle ignore
					while ( ignore && audio_used > *samples )
					{
						ignore --;
						audio_used -= *samples;
						memmove( audio_buffer, &audio_buffer[ *samples * *channels ], audio_used * sizeof( int16_t ) );
					}
				}

				// If we're behind, ignore this packet
				if ( pkt.pts >= 0 )
				{
					double current_pts = av_q2d( stream->time_base ) * pkt.pts;
					double source_fps = mlt_properties_get_double( properties, "source_fps" );
					int req_position = ( int )( real_timecode * source_fps + 0.5 );
					int int_position = ( int )( current_pts * source_fps + 0.5 );

					if ( context->start_time != AV_NOPTS_VALUE )
						int_position -= ( int )( context->start_time * source_fps / AV_TIME_BASE + 0.5 );
					if ( seekable && !ignore && int_position < req_position )
						ignore = 1;
				}
			}

			// We're finished with this packet regardless
			av_free_packet( &pkt );
		}

		int size = *samples * *channels * sizeof( int16_t );
		*format = mlt_audio_s16;
		*buffer = mlt_pool_alloc( size );
		mlt_frame_set_audio( frame, *buffer, *format, size, mlt_pool_release );

		// Now handle the audio if we have enough
		if ( audio_used >= *samples )
		{
			memcpy( *buffer, audio_buffer, *samples * *channels * sizeof( int16_t ) );
			audio_used -= *samples;
			memmove( audio_buffer, &audio_buffer[ *samples * *channels ], audio_used * *channels * sizeof( int16_t ) );
		}
		else
		{
			memset( *buffer, 0, *samples * *channels * sizeof( int16_t ) );
		}

		// Store the number of audio samples still available
		mlt_properties_set_int( properties, "_audio_used", audio_used );
	}
	else
	{
		// Get silence and don't touch the context
		mlt_frame_get_audio( frame, buffer, format, frequency, channels, samples );
	}

	// Regardless of speed (other than paused), we expect to get the next frame
	if ( !paused )
		mlt_properties_set_position( properties, "_audio_expected", position + 1 );

	return 0;
}

/** Set up audio handling.
*/

static void producer_set_up_audio( mlt_producer this, mlt_frame frame )
{
	// Get the properties
	mlt_properties properties = MLT_PRODUCER_PROPERTIES( this );

	// Fetch the audio_context
	AVFormatContext *context = mlt_properties_get_data( properties, "audio_context", NULL );

	// Get the audio_index
	int index = mlt_properties_get_int( properties, "audio_index" );

	// Reopen the file if necessary
	if ( !context && index > -1 )
	{
		mlt_events_block( properties, this );
		producer_open( this, mlt_service_profile( MLT_PRODUCER_SERVICE(this) ),
			mlt_properties_get( properties, "resource" ) );
		context = mlt_properties_get_data( properties, "audio_context", NULL );
		mlt_properties_set_data( properties, "dummy_context", NULL, 0, NULL, NULL );
		mlt_events_unblock( properties, this );
	}

	// Exception handling for audio_index
	if ( context && index >= (int) context->nb_streams )
	{
		for ( index = context->nb_streams - 1; index >= 0 && context->streams[ index ]->codec->codec_type != CODEC_TYPE_AUDIO; --index );
		mlt_properties_set_int( properties, "audio_index", index );
	}
	if ( context && index > -1 && context->streams[ index ]->codec->codec_type != CODEC_TYPE_AUDIO )
	{
		index = -1;
		mlt_properties_set_int( properties, "audio_index", index );
	}

	// Update the audio properties if the index changed
	if ( index > -1 && index != mlt_properties_get_int( properties, "_audio_index" ) )
	{
		mlt_properties_set_int( properties, "_audio_index", index );
		mlt_properties_set_data( properties, "audio_codec", NULL, 0, NULL, NULL );
	}

	// Deal with audio context
	if ( context != NULL && index > -1 )
	{
		// Get the frame properties
		mlt_properties frame_properties = MLT_FRAME_PROPERTIES( frame );

		// Get the audio stream
		AVStream *stream = context->streams[ index ];

		// Get codec context
		AVCodecContext *codec_context = stream->codec;

		// Get the codec
		AVCodec *codec = mlt_properties_get_data( properties, "audio_codec", NULL );

		// Initialise the codec if necessary
		if ( codec == NULL )
		{
			// Find the codec
			codec = avcodec_find_decoder( codec_context->codec_id );

			// If we don't have a codec and we can't initialise it, we can't do much more...
			avformat_lock( );
			if ( codec != NULL && avcodec_open( codec_context, codec ) >= 0 )
			{
				// Now store the codec with its destructor
				mlt_properties_set_data( properties, "audio_codec", codec_context, 0, producer_codec_close, NULL );

			}
			else
			{
				// Remember that we can't use this later
				mlt_properties_set_int( properties, "audio_index", -1 );
				index = -1;
			}
			avformat_unlock( );

			// Process properties as AVOptions
			apply_properties( codec_context, properties, AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_DECODING_PARAM );
		}

		// No codec, no show...
		if ( codec && index > -1 )
		{
			mlt_frame_push_audio( frame, producer_get_audio );
			mlt_properties_set_data( frame_properties, "avformat_producer", this, 0, NULL, NULL );
			mlt_properties_set_int( frame_properties, "frequency", codec_context->sample_rate );
			mlt_properties_set_int( frame_properties, "channels", codec_context->channels );
		}
	}
}

/** Our get frame implementation.
*/

static int producer_get_frame( mlt_producer this, mlt_frame_ptr frame, int index )
{
	// Create an empty frame
	*frame = mlt_frame_init( MLT_PRODUCER_SERVICE( this ) );

	// Update timecode on the frame we're creating
	mlt_frame_set_position( *frame, mlt_producer_position( this ) );

	// Set the position of this producer
	mlt_properties_set_position( MLT_FRAME_PROPERTIES( *frame ), "avformat_position", mlt_producer_frame( this ) );

	// Set up the video
	producer_set_up_video( this, *frame );

	// Set up the audio
	producer_set_up_audio( this, *frame );

	// Set the aspect_ratio
	mlt_properties_set_double( MLT_FRAME_PROPERTIES( *frame ), "aspect_ratio", mlt_properties_get_double( MLT_PRODUCER_PROPERTIES( this ), "aspect_ratio" ) );

	// Calculate the next timecode
	mlt_producer_prepare_next( this );

	return 0;
}
