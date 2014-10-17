/*
 * consumer_cbrts.c -- output constant bitrate MPEG-2 transport stream
 *
 * Copyright (C) 2010-2014 Broadcasting Center Europe S.A. http://www.bce.lu
 * an RTL Group Company  http://www.rtlgroup.com
 * Author: Dan Dennedy <dan@dennedy.org>
 * Some ideas and portions come from OpenCaster, Copyright (C) Lorenzo Pallara <l.pallara@avalpa.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <framework/mlt.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#ifdef WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif
#include <strings.h>

#define TSP_BYTES     (188)
#define MAX_PID       (8192)
#define SCR_HZ        (27000000ULL)
#define NULL_PID      (0x1fff)
#define PAT_PID       (0)
#define SDT_PID       (0x11)
#define PCR_SMOOTHING (12)
#define PCR_PERIOD_MS (20)

#define PIDOF( packet )  ( ntohs( *( ( uint16_t* )( packet + 1 ) ) ) & 0x1fff )
#define HASPCR( packet ) ( (packet[3] & 0x20) && (packet[4] != 0) && (packet[5] & 0x10) )
#define CCOF( packet ) ( packet[3] & 0x0f )
#define ADAPTOF( packet ) ( ( packet[3] >> 4 ) & 0x03 )

typedef struct consumer_cbrts_s *consumer_cbrts;

struct consumer_cbrts_s
{
	struct mlt_consumer_s parent;
	mlt_consumer avformat;
	pthread_t thread;
	int joined;
	int running;
	mlt_position last_position;
	mlt_event event_registered;
	int fd;
	uint8_t *leftover_data[TSP_BYTES];
	int leftover_size;
	mlt_deque packets;
	mlt_deque packets2;
	uint64_t previous_pcr;
	uint64_t previous_packet_count;
	uint64_t packet_count;
	int is_stuffing_set;
	pthread_t remux_thread;
	pthread_mutex_t deque_mutex;
	pthread_cond_t deque_cond;
	int is_remuxing;
	uint8_t pcr_count;
	uint16_t pmt_pid;
	int is_si_sdt;
	int is_si_pat;
	int is_si_pmt;
	int dropped;
	uint8_t continuity_count[MAX_PID];
	uint64_t output_counter;
};

typedef struct {
	int size;
	int period;
	int packet_count;
	uint16_t pid;
	uint8_t data[4096];
} ts_section;

static uint8_t null_packet[ TSP_BYTES ];

/** Forward references to static functions.
*/

static int consumer_start( mlt_consumer parent );
static int consumer_stop( mlt_consumer parent );
static int consumer_is_stopped( mlt_consumer parent );
static void consumer_close( mlt_consumer parent );
static void *consumer_thread( void * );
static void on_data_received( mlt_properties properties, mlt_consumer consumer, uint8_t *buf, int size );

mlt_consumer consumer_cbrts_init( mlt_profile profile, mlt_service_type type, const char *id, char *arg )
{
	consumer_cbrts self = calloc( sizeof( struct consumer_cbrts_s ), 1 );
	if ( self && mlt_consumer_init( &self->parent, self, profile ) == 0 )
	{
		// Get the parent consumer object
		mlt_consumer parent = &self->parent;

		// Get the properties
		mlt_properties properties = MLT_CONSUMER_PROPERTIES( parent );

		// Create child consumers
		self->avformat = mlt_factory_consumer( profile, "avformat", NULL );
		parent->close = consumer_close;
		parent->start = consumer_start;
		parent->stop = consumer_stop;
		parent->is_stopped = consumer_is_stopped;
		self->joined = 1;
		self->packets = mlt_deque_init();
		self->packets2 = mlt_deque_init();

		// Create the null packet
		memset( null_packet, 0xFF, TSP_BYTES );
		null_packet[0] = 0x47;
		null_packet[1] = 0x1f;
		null_packet[2] = 0xff;
		null_packet[3] = 0x10;

		// Create the deque mutex and condition
		pthread_mutex_init( &self->deque_mutex, NULL );
		pthread_cond_init( &self->deque_cond, NULL );

		// Set consumer property defaults
		mlt_properties_set_int( properties, "real_time", -1 );

		return parent;
	}
	free( self );
	return NULL;
}

static ts_section* load_section( const char *filename )
{
	ts_section *section = NULL;
	int fd;

	if ( !filename )
		return NULL;
	if ( ( fd = open( filename, O_RDONLY ) ) < 0 )
	{
		mlt_log_error( NULL, "cbrts consumer failed to load section file %s\n", filename );
		return NULL;
	}

	section = malloc( sizeof(*section) );
	memset( section, 0xff, sizeof(*section) );
	section->size = 0;

	if ( read( fd, section->data, 3 ) )
	{
		// get the size
		uint16_t *p = (uint16_t*) &section->data[1];
		section->size = p[0];
		section->size = ntohs( section->size ) & 0x0FFF;

		// read the data
		if ( section->size <= sizeof(section->data) - 3 )
		{
			ssize_t has_read = 0;
			while ( has_read < section->size )
			{
				ssize_t n = read( fd, section->data + 3 + has_read, section->size );
				if ( n > 0 )
					has_read += n;
				else
					break;
			}
			section->size += 3;
		}
		else
		{
			mlt_log_error( NULL, "Section too big - skipped.\n" );
		}
	}
	close( fd );

	return section;
}

static void load_sections( consumer_cbrts self, mlt_properties properties )
{
	int n = mlt_properties_count( properties );

	// Store the sections with automatic cleanup
	// and make it easy to iterate over the data sections.
	mlt_properties si_properties = mlt_properties_get_data( properties, "si.properties", NULL );
	if ( !si_properties )
	{
		si_properties = mlt_properties_new();
		mlt_properties_set_data( properties, "si.properties", si_properties, 0, (mlt_destructor) mlt_properties_close, NULL );
	}

	while ( n-- )
	{
		// Look for si.<name>.file=filename
		const char *name = mlt_properties_get_name( properties, n );
		if ( strncmp( "si.", name, 3 ) == 0
		  && strncmp( ".file", name + strlen( name ) - 5, 5 ) == 0 )
		{
			size_t len = strlen( name );
			char *si_name = strdup( name + 3 );
			char si_pid[len + 1];

			si_name[len - 3 - 5] = 0;
			strcpy( si_pid, "si." );
			strcat( si_pid, si_name );
			strcat( si_pid, ".pid" );

			// Look for si.<name>.pid=<number>
			if ( mlt_properties_get( properties, si_pid ) )
			{
				char *filename = mlt_properties_get_value( properties, n );

				ts_section *section = load_section( filename );
				if ( section )
				{
					// Determine the periodicity of the section, if supplied
					char si_time[len + 1];

					strcpy( si_time, "si." );
					strcat( si_time, si_name );
					strcat( si_time, ".time" );

					int time = mlt_properties_get_int( properties, si_time );
					if ( time == 0 )
						time = 200;

					// Set flags if we are replacing PAT or SDT
					if ( strncasecmp( "pat", si_name, 3) == 0 )
						self->is_si_pat = 1;
					else if ( strncasecmp( "pmt", si_name, 3) == 0 )
						self->is_si_pmt = 1;
					else if ( strncasecmp( "sdt", si_name, 3) == 0 )
						self->is_si_sdt = 1;

					// Calculate the period and get the PID
					uint64_t muxrate = mlt_properties_get_int( properties, "muxrate" );
					section->period = ( muxrate * time ) / ( TSP_BYTES * 8 * 1000 );
					// output one immediately
					section->packet_count = section->period - 1;
					mlt_log_verbose( NULL, "SI %s time=%d period=%d file=%s\n", si_name, time, section->period, filename );
					section->pid = mlt_properties_get_int( properties, si_pid );

					mlt_properties_set_data( si_properties, si_name, section, section->size, free, NULL );
				}
			}
			free( si_name );
		}
	}
}

static void write_section( consumer_cbrts self, ts_section *section )
{
	uint8_t *packet;
	const uint8_t *data_ptr = section->data;
	uint8_t *p;
	int size = section->size;
	int first, len;

	while ( size > 0 )
	{
		first = ( section->data == data_ptr );
		p = packet = malloc( TSP_BYTES );
		*p++ = 0x47;
		*p = ( section->pid >> 8 );
		if ( first )
			*p |= 0x40;
		p++;
		*p++ = section->pid;
		*p++ = 0x10; // continuity count will be written later
		if ( first )
			*p++ = 0; /* 0 offset */
		len = TSP_BYTES - ( p - packet );
		if ( len > size )
			len = size;
		memcpy( p, data_ptr, len );
		p += len;
		/* add known padding data */
		len = TSP_BYTES - ( p - packet );
		if ( len > 0 )
			memset( p, 0xff, len );

		mlt_deque_push_back( self->packets2, packet );
		self->packet_count++;

		data_ptr += len;
		size -= len;
	}
}

static void write_sections( consumer_cbrts self )
{
	mlt_properties properties = mlt_properties_get_data( MLT_CONSUMER_PROPERTIES( &self->parent ), "si.properties", NULL );

	if ( properties )
	{
		int n = mlt_properties_count( properties );
		while ( n-- )
		{
			ts_section *section = mlt_properties_get_data_at( properties, n, NULL );
			if ( ++section->packet_count == section->period )
			{
				section->packet_count = 0;
				write_section( self, section );
			}
		}
	}
}

static uint64_t get_pcr( uint8_t *packet )
{
	uint64_t pcr = 0;
	pcr += (uint64_t) packet[6] << 25;
	pcr += packet[7] << 17;
	pcr += packet[8] << 9;
	pcr += packet[9] << 1;
	pcr += packet[10] >> 7;
	pcr *= 300; // convert 90KHz to 27MHz
	pcr += (packet[10] & 0x01) << 8;
	pcr += packet[11];
	return pcr;
}

static void set_pcr( uint8_t *packet, uint64_t pcr )
{
	uint64_t pcr_base = pcr / 300;
	uint64_t pcr_ext = pcr % 300;
	packet[6]  = pcr_base >> 25;
	packet[7]  = pcr_base >> 17;
	packet[8]  = pcr_base >> 9;
	packet[9]  = pcr_base >> 1;
	packet[10] = (( pcr_base & 1 ) << 7) | 0x7E | (( pcr_ext & 0x100 ) >> 8);
	packet[11] = pcr_ext;
}

static uint64_t update_pcr( consumer_cbrts self, uint64_t muxrate, unsigned packets )
{
	return self->previous_pcr + packets * TSP_BYTES * 8 * SCR_HZ / muxrate;
}

static double measure_bitrate( consumer_cbrts self, uint64_t pcr, int drop )
{
	double muxrate = 0;

	if ( self->is_stuffing_set || self->previous_pcr )
	{
		muxrate = ( self->packet_count - self->previous_packet_count - drop ) * TSP_BYTES * 8;
		if ( pcr >= self->previous_pcr )
			muxrate /= (double) ( pcr - self->previous_pcr ) / SCR_HZ;
		else
			muxrate /= ( ( (double) ( 1ULL << 33 ) - 1 ) * 300 - self->previous_pcr + pcr ) / SCR_HZ;
		mlt_log_debug( NULL, "measured TS bitrate %.1f bits/sec PCR %"PRIu64"\n", muxrate, pcr );
	}

	return muxrate;
}

static int writen( int fd, const void *buf, size_t count )
{
	int result = 0;
	int written = 0;
	while ( written < count )
	{
		if ( ( result = write( fd, buf + written, count - written ) ) < 0 )
		{
			mlt_log_error( NULL, "Failed to write: %s\n", strerror( errno ) );
			break;
		}
		written += result;
	}
	return result;
}

static int insert_pcr( consumer_cbrts self, uint16_t pid, uint8_t cc, uint64_t pcr )
{
	uint8_t packet[ TSP_BYTES ];
    uint8_t *p = packet;

    *p++ = 0x47;
    *p++ = pid >> 8;
    *p++ = pid;
    *p++ = 0x20 | cc;     // Adaptation only
    // Continuity Count field does not increment (see 13818-1 section 2.4.3.3)
    *p++ = TSP_BYTES - 5; // Adaptation Field Length
    *p++ = 0x10;          // Adaptation flags: PCR present
	set_pcr( packet, pcr );
	p += 6; // 6 pcr bytes
    memset( p, 0xff, TSP_BYTES - ( p - packet ) ); // stuffing

	return writen( self->fd, packet, TSP_BYTES );
}

static int output_cbr( consumer_cbrts self, uint64_t input_rate, uint64_t output_rate, uint64_t *pcr )
{
	int n = mlt_deque_count( self->packets2 );
	unsigned output_packets = 0;
	unsigned packets_since_pcr = 0;
	int result = 0;
	int dropped = 0;
	int warned = 0;
	uint16_t pcr_pid = 0;
	uint8_t cc = 0xff;
	float ms_since_pcr;
	float ms_to_end;
	uint64_t input_counter = 0;
	uint64_t last_input_counter;

	mlt_log_debug( NULL, "%s: n %i output_counter %"PRIu64" input_rate %"PRIu64"\n", __FUNCTION__, n, self->output_counter, input_rate );
	while ( n-- && result >= 0 )
	{
		uint8_t *packet = mlt_deque_pop_front( self->packets2 );
		uint16_t pid = PIDOF( packet );

		// Check for overflow
		if ( input_rate > output_rate && !HASPCR( packet ) &&
		     pid != SDT_PID && pid != PAT_PID && pid != self->pmt_pid )
		{
			if ( !warned )
			{
				mlt_log_warning( MLT_CONSUMER_SERVICE( &self->parent ), "muxrate too low %"PRIu64" > %"PRIu64"\n", input_rate, output_rate );
				warned = 1;
			}

			// Skip this packet
			free( packet );

			// Compute new input_rate based on dropped count
			input_rate = measure_bitrate( self, *pcr, ++dropped );

			continue;
		}

		if ( HASPCR( packet ) )
		{
			pcr_pid = pid;
			set_pcr( packet, update_pcr( self, output_rate, output_packets ) );
			packets_since_pcr = 0;
		}

		// Rewrite the continuity counter if not only adaptation field
		if ( ADAPTOF( packet ) != 2 )
		{
			packet[3] = ( packet[3] & 0xf0 ) | self->continuity_count[ pid ];
			self->continuity_count[ pid ] = ( self->continuity_count[ pid ] + 1 ) & 0xf;
		}
		if ( pcr_pid && pid == pcr_pid )
			cc = CCOF( packet );

		result = writen( self->fd, packet, TSP_BYTES );
		free( packet );
		if ( result < 0 )
			break;
		output_packets++;
		packets_since_pcr++;
		self->output_counter += TSP_BYTES * 8 * output_rate;
		input_counter += TSP_BYTES * 8 * input_rate;

		// See if we need to output a dummy packet with PCR
		ms_since_pcr = (float) ( packets_since_pcr + 1 ) * 8 * TSP_BYTES * 1000 / output_rate;
		ms_to_end = (float) n * 8 * TSP_BYTES * 1000 / input_rate;
		if ( pcr_pid && ms_since_pcr >= PCR_PERIOD_MS && ms_to_end > PCR_PERIOD_MS / 2.0 )
		{
			uint64_t new_pcr = update_pcr( self, output_rate, output_packets );
			if ( ms_since_pcr > 40 )
				mlt_log_warning( NULL, "exceeded PCR interval %.2f ms queued %.2f ms\n", ms_since_pcr, ms_to_end );
			if ( ( result = insert_pcr( self, pcr_pid, cc, new_pcr ) ) < 0 )
				break;
			packets_since_pcr = 0;
			output_packets++;
			input_counter += TSP_BYTES * 8 * input_rate;
		}

		// Output null packets as needed
		while ( input_counter + ( TSP_BYTES * 8 * input_rate ) <= self->output_counter )
		{
			// See if we need to output a dummy packet with PCR
			ms_since_pcr = (float) ( packets_since_pcr + 1 ) * 8 * TSP_BYTES * 1000 / output_rate;
			ms_to_end = (float) n * 8 * TSP_BYTES * 1000 / input_rate;
            
			if ( pcr_pid && ms_since_pcr >= PCR_PERIOD_MS && ms_to_end > PCR_PERIOD_MS / 2.0 )
			{
				uint64_t new_pcr = update_pcr( self, output_rate, output_packets );
				if ( ms_since_pcr > 40 )
					mlt_log_warning( NULL, "exceeded PCR interval %.2f ms queued %.2f ms\n", ms_since_pcr, ms_to_end );
				if ( ( result = insert_pcr( self, pcr_pid, cc, new_pcr ) ) < 0 )
					break;
				packets_since_pcr = 0;
			}
			else
			{
				// Otherwise output a null packet
				if ( ( result = writen( self->fd, null_packet, TSP_BYTES ) ) < 0 )
					break;
				packets_since_pcr++;
			}
			output_packets++;

			// Increment input
			last_input_counter = input_counter;
			input_counter += TSP_BYTES * 8 * input_rate;

			// Handle wrapping
			if ( last_input_counter > input_counter )
			{
				last_input_counter -= self->output_counter;
				self->output_counter = 0;
				input_counter = last_input_counter + TSP_BYTES * 8 * input_rate;
			}
		}
	}

	// Reset counters leaving a residual output count
	if ( input_counter < self->output_counter )
		self->output_counter -= input_counter;
	else
		self->output_counter = 0;

	// Warn if the PCR interval is too large or small
	ms_since_pcr = (float) packets_since_pcr * 8 * TSP_BYTES * 1000 / output_rate;
	if ( ms_since_pcr > 40 )
		mlt_log_warning( NULL, "exceeded PCR interval %.2f ms\n", ms_since_pcr );
	else if ( ms_since_pcr < PCR_PERIOD_MS / 2.0 )
		mlt_log_debug( NULL, "PCR interval too short %.2f ms\n", ms_since_pcr );

	// Update the current PCR based on number of packets output
	*pcr = update_pcr( self, output_rate, output_packets );

	return result;
}

static void get_pmt_pid( consumer_cbrts self, uint8_t *packet )
{
	// Skip 5 bytes of TSP header + 8 bytes of section header + 2 bytes of service ID
	uint16_t *p = ( uint16_t* )( packet + 5 + 8 + 2 );
	self->pmt_pid = ntohs( p[0] ) & 0x1fff;
	mlt_log_debug(NULL, "PMT PID 0x%04x\n", self->pmt_pid );
	return;
}

static void *remux_thread( void *arg )
{
	consumer_cbrts self = arg;
	mlt_service service = MLT_CONSUMER_SERVICE( &self->parent );
	mlt_properties properties = MLT_CONSUMER_PROPERTIES( &self->parent );
	double output_rate = mlt_properties_get_int( properties, "muxrate" );
	int remux = !mlt_properties_get_int( properties, "noremux" );
	int i;
	int result = 0;

	while ( self->is_remuxing )
	{
		pthread_mutex_lock( &self->deque_mutex );
		while ( self->is_remuxing && mlt_deque_count( self->packets ) < 10 )
			pthread_cond_wait( &self->deque_cond, &self->deque_mutex );
		pthread_mutex_unlock( &self->deque_mutex );

		// Dequeue the packets and write them
		i = mlt_deque_count( self->packets );
		mlt_log_debug( service, "%s: count %d\n", __FUNCTION__, i );
		while ( self->is_remuxing && i-- && result >= 0 )
		{
			if ( remux )
				write_sections( self );

			pthread_mutex_lock( &self->deque_mutex );
			uint8_t *packet = mlt_deque_pop_front( self->packets );
			pthread_mutex_unlock( &self->deque_mutex );
			uint16_t pid = PIDOF( packet );

			// Sanity checks
			if ( packet[0] != 0x47 )
			{
				mlt_log_panic( service, "NOT SYNC BYTE 0x%02x\n", packet[0] );
				exit(1);
			}
			if ( remux && pid == NULL_PID )
			{
				mlt_log_panic( service, "NULL PACKET\n" );
				exit(1);
			}

			// Measure the bitrate between consecutive PCRs
			if ( HASPCR( packet ) )
			{
				if ( self->pcr_count++ % PCR_SMOOTHING == 0 )
				{
					uint64_t pcr = get_pcr( packet );
					double input_rate = measure_bitrate( self, pcr, 0 );
					if ( input_rate > 0 )
					{
						self->is_stuffing_set = 1;
						if ( remux && input_rate > 1.0 )
						{
							result = output_cbr( self, input_rate, output_rate, &pcr );
							set_pcr( packet, pcr );
						}
					}
					self->previous_pcr = pcr;
					self->previous_packet_count = self->packet_count;
				}
			}
			if ( remux )
			{
				mlt_deque_push_back( self->packets2, packet );
			}
			else
			{
				if ( self->is_stuffing_set )
					result = writen( self->fd, packet, TSP_BYTES );
				free( packet );
			}
			self->packet_count++;
		}
	}
	return NULL;
}

static void start_remux_thread( consumer_cbrts self )
{
	self->is_remuxing = 1;
	int rtprio = mlt_properties_get_int( MLT_CONSUMER_PROPERTIES( &self->parent ), "rtprio" );
	if ( rtprio > 0 )
	{
		// Use realtime priority class
		struct sched_param priority;
		pthread_attr_t thread_attributes;
		pthread_attr_init( &thread_attributes );
		priority.sched_priority = rtprio;
		pthread_attr_setschedpolicy( &thread_attributes, SCHED_FIFO );
		pthread_attr_setschedparam( &thread_attributes, &priority );
		pthread_attr_setinheritsched( &thread_attributes, PTHREAD_EXPLICIT_SCHED );
		pthread_attr_setscope( &thread_attributes, PTHREAD_SCOPE_SYSTEM );
		if ( pthread_create( &self->remux_thread, &thread_attributes, remux_thread, self ) < 0 )
		{
			mlt_log_info( MLT_CONSUMER_SERVICE( &self->parent ),
				"failed to initialize remux thread with realtime priority\n" );
			pthread_create( &self->remux_thread, &thread_attributes, remux_thread, self );
		}
		pthread_attr_destroy( &thread_attributes );
	}
	else
	{
		// Use normal priority class
		pthread_create( &self->remux_thread, NULL, remux_thread, self );
	}
}

static void stop_remux_thread( consumer_cbrts self )
{
	if ( self->is_remuxing )
	{
		self->is_remuxing = 0;

		// Broadcast to the condition in case it's waiting
		pthread_mutex_lock( &self->deque_mutex );
		int n = mlt_deque_count( self->packets );
		while ( n-- )
			free( mlt_deque_pop_back( self->packets ) );
		pthread_cond_broadcast( &self->deque_cond );
		pthread_mutex_unlock( &self->deque_mutex );

		// Join the thread
		pthread_join( self->remux_thread, NULL );

		n = mlt_deque_count( self->packets2 );
		while ( n-- )
			free( mlt_deque_pop_back( self->packets2 ) );
	}
}

static inline int filter_packet( consumer_cbrts self, uint8_t *packet )
{
	uint16_t pid = PIDOF( packet );

	// We are going to keep the existing PMT; replace all other signaling sections.
	int result = ( pid == NULL_PID )
	          || ( pid == PAT_PID && self->is_si_pat )
	          || ( pid == self->pmt_pid && self->is_si_pmt )
	          || ( pid == SDT_PID && self->is_si_sdt );

	// Get the PMT PID from the PAT
	if ( pid == PAT_PID && !self->pmt_pid )
	{
		get_pmt_pid( self, packet );
		if ( self->is_si_pmt )
			result = 1;
	}
	
	return result;
}

static void on_data_received( mlt_properties properties, mlt_consumer consumer, uint8_t *buf, int size )
{
	if ( size > 0 )
	{
		consumer_cbrts self = (consumer_cbrts) consumer->child;
		mlt_service service = MLT_CONSUMER_SERVICE( consumer );
		mlt_properties properties = MLT_CONSUMER_PROPERTIES( consumer );
		int noremux = mlt_properties_get_int( properties, "noremux" );

		// Sanity check
		if ( self->leftover_size == 0 && buf[0] != 0x47 )
		{
			mlt_log_verbose(MLT_CONSUMER_SERVICE( consumer ), "NOT SYNC BYTE 0x%02x\n", buf[0] );
			while ( size && buf[0] != 0x47 )
			{
				buf++;
				size--;
			}
			if ( size <= 0 )
				exit(1);
		}

		// Enqueue the packets
		int num_packets = ( self->leftover_size + size ) / TSP_BYTES;
		int remaining = ( self->leftover_size + size ) % TSP_BYTES;
		uint8_t *packet = NULL;
		int i;

//			mlt_log_verbose( service, "%s: packets %d remaining %i\n", __FUNCTION__, num_packets, self->leftover_size );

		pthread_mutex_lock( &self->deque_mutex );
		if ( self->leftover_size )
		{
			packet = malloc( TSP_BYTES );
			memcpy( packet, self->leftover_data, self->leftover_size );
			memcpy( packet + self->leftover_size, buf, TSP_BYTES - self->leftover_size );
			buf += TSP_BYTES - self->leftover_size;
			--num_packets;

			// Filter out null packets
			if ( noremux || !filter_packet( self, packet ) )
				mlt_deque_push_back( self->packets, packet );
			else
				free( packet );
		}
		for ( i = 0; i < num_packets; i++, buf += TSP_BYTES )
		{
			packet = malloc( TSP_BYTES );
			memcpy( packet, buf, TSP_BYTES );

			// Filter out null packets
			if ( noremux || !filter_packet( self, packet ) )
				mlt_deque_push_back( self->packets, packet );
			else
				free( packet );
		}
		pthread_cond_broadcast( &self->deque_cond );
		pthread_mutex_unlock( &self->deque_mutex );

		self->leftover_size = remaining;
		memcpy( self->leftover_data, buf, self->leftover_size );

		if ( !self->is_remuxing )
			start_remux_thread( self );
		mlt_log_debug( service, "%s: %p 0x%x (%d)\n", __FUNCTION__, buf, *buf, size % TSP_BYTES );

		// Do direct output
//		result = writen( self->fd, buf, size );
	}
}

static int consumer_start( mlt_consumer parent )
{
	consumer_cbrts self = parent->child;

	if ( !self->running )
	{
		mlt_properties properties = MLT_CONSUMER_PROPERTIES( parent );
		mlt_properties avformat = MLT_CONSUMER_PROPERTIES(self->avformat);

		// Cleanup after a possible abort
		consumer_stop( parent );

		// Pass properties down
		mlt_properties_pass( avformat, properties, "" );
		mlt_properties_set_data( avformat, "app_lock", mlt_properties_get_data( properties, "app_lock", NULL ), 0, NULL, NULL );
		mlt_properties_set_data( avformat, "app_unlock", mlt_properties_get_data( properties, "app_unlock", NULL ), 0, NULL, NULL );
		mlt_properties_set_int( avformat, "put_mode", 1 );
		mlt_properties_set_int( avformat, "real_time", -1 );
		mlt_properties_set_int( avformat, "buffer", 2 );
		mlt_properties_set_int( avformat, "terminate_on_pause", 0 );
		mlt_properties_set_int( avformat, "muxrate", 1 );
		mlt_properties_set_int( avformat, "redirect", 1 );
		mlt_properties_set( avformat, "f", "mpegts" );
		self->dropped = 0;
		self->fd = STDOUT_FILENO;

		// Load the DVB PSI/SI sections
		load_sections( self, properties );

		// Start the FFmpeg consumer and our thread
		mlt_consumer_start( self->avformat );
		pthread_create( &self->thread, NULL, consumer_thread, self );
		self->running = 1;
		self->joined = 0;
	}

	return 0;
}

static int consumer_stop( mlt_consumer parent )
{
	// Get the actual object
	consumer_cbrts self = parent->child;

	if ( !self->joined )
	{
		mlt_properties properties = MLT_CONSUMER_PROPERTIES( parent );
		int app_locked = mlt_properties_get_int( properties, "app_locked" );
		void ( *lock )( void ) = mlt_properties_get_data( properties, "app_lock", NULL );
		void ( *unlock )( void ) = mlt_properties_get_data( properties, "app_unlock", NULL );

		if ( app_locked && unlock ) unlock( );

		// Kill the threads and clean up
		self->running = 0;
#ifndef WIN32
		if ( self->thread )
#endif
			pthread_join( self->thread, NULL );
		self->joined = 1;

		if ( self->avformat )
			mlt_consumer_stop( self->avformat );
		stop_remux_thread( self );
		if ( self->fd > 1 )
			close( self->fd );

		if ( app_locked && lock ) lock( );
	}
	return 0;
}

static int consumer_is_stopped( mlt_consumer parent )
{
	consumer_cbrts self = parent->child;
	return !self->running;
}

static void *consumer_thread( void *arg )
{
	// Identify the arg
	consumer_cbrts self = arg;

	// Get the consumer and producer
	mlt_consumer consumer = &self->parent;

	// Get the properties
	mlt_properties properties = MLT_CONSUMER_PROPERTIES( consumer );

	// internal intialization
	mlt_frame frame = NULL;
	int last_position = -1;

	// Loop until told not to
	while( self->running )
	{
		// Get a frame from the attached producer
		frame = mlt_consumer_rt_frame( consumer );

		// Ensure that we have a frame
		if ( self->running && frame )
		{
			if ( mlt_properties_get_int( MLT_FRAME_PROPERTIES( frame ), "rendered" ) != 1 )
			{
				mlt_frame_close( frame );
				mlt_log_warning( MLT_CONSUMER_SERVICE(consumer), "dropped frame %d\n", ++self->dropped );
				continue;
			}

			// Get the speed of the frame
			double speed = mlt_properties_get_double( MLT_FRAME_PROPERTIES( frame ), "_speed" );

			// Optimisation to reduce latency
			if ( speed == 1.0 )
			{
				if ( last_position != -1 && last_position + 1 != mlt_frame_get_position( frame ) )
					mlt_consumer_purge( self->avformat );
				last_position = mlt_frame_get_position( frame );
			}
			else
			{
				//mlt_consumer_purge( this->play );
				last_position = -1;
			}

			mlt_consumer_put_frame( self->avformat, frame );
			mlt_events_fire( properties, "consumer-frame-show", frame, NULL );

			// Setup event listener as a callback from consumer avformat
			if ( !self->event_registered )
				self->event_registered = mlt_events_listen( MLT_CONSUMER_PROPERTIES( self->avformat ),
					consumer, "avformat-write", (mlt_listener) on_data_received );
		}
		else
		{
			if ( frame ) mlt_frame_close( frame );
			mlt_consumer_put_frame( self->avformat, NULL );
			self->running = 0;
		}
	}
	return NULL;
}

/** Callback to allow override of the close method.
*/

static void consumer_close( mlt_consumer parent )
{
	// Get the actual object
	consumer_cbrts self = parent->child;

	// Stop the consumer
	mlt_consumer_stop( parent );

	// Close the child consumers
	mlt_consumer_close( self->avformat );

	// Now clean up the rest
	mlt_deque_close( self->packets );
	mlt_deque_close( self->packets2 );
	mlt_consumer_close( parent );

	// Finally clean up this
	free( self );
}
