/*
 * miracle_server.c
 * Copyright (C) 2002-2003 Ushodaya Enterprises Limited
 * Author: Charles Yates <charles.yates@pandora.be>
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

/* System header files */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

#include <string.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <arpa/inet.h>

/* Application header files */
#include "miracle_server.h"
#include "miracle_connection.h"
#include "miracle_local.h"
#include "miracle_log.h"
#include "miracle_commands.h"
#include <valerie/valerie_remote.h>
#include <valerie/valerie_tokeniser.h>

#define VERSION "0.0.1"

static void miracle_command_received( mlt_listener listener, mlt_properties owner, miracle_server this, void **args )
{
	if ( listener != NULL )
		listener( owner, this, ( valerie_response ** )args[ 0 ], ( char * )args[ 1 ] );
}

static void miracle_doc_received( mlt_listener listener, mlt_properties owner, miracle_server this, void **args )
{
	if ( listener != NULL )
		listener( owner, this, ( valerie_response ** )args[ 0 ], ( char * )args[ 1 ], ( char * )args[ 2 ] );
}

static void miracle_push_received( mlt_listener listener, mlt_properties owner, miracle_server this, void **args )
{
	if ( listener != NULL )
		listener( owner, this, ( valerie_response ** )args[ 0 ], ( char * )args[ 1 ], ( mlt_service )args[ 2 ] );
}

/** Initialise a server structure.
*/

miracle_server miracle_server_init( char *id )
{
	miracle_server server = malloc( sizeof( miracle_server_t ) );
	if ( server != NULL )
		memset( server, 0, sizeof( miracle_server_t ) );
	if ( server != NULL && mlt_properties_init( &server->parent, server ) == 0 )
	{
		server->id = id;
		server->port = DEFAULT_TCP_PORT;
		server->socket = -1;
		server->shutdown = 1;
		mlt_events_init( &server->parent );
		mlt_events_register( &server->parent, "command-received", ( mlt_transmitter )miracle_command_received );
		mlt_events_register( &server->parent, "doc-received", ( mlt_transmitter )miracle_doc_received );
		mlt_events_register( &server->parent, "push-received", ( mlt_transmitter )miracle_push_received );
	}
	return server;
}

const char *miracle_server_id( miracle_server server )
{
	return server != NULL && server->id != NULL ? server->id : "miracle";
}

void miracle_server_set_config( miracle_server server, const char *config )
{
	if ( server != NULL )
	{
		free( server->config );
		server->config = config != NULL ? strdup( config ) : NULL;
	}
}

/** Set the port of the server.
*/

void miracle_server_set_port( miracle_server server, int port )
{
	server->port = port;
}

void miracle_server_set_proxy( miracle_server server, char *proxy )
{
	valerie_tokeniser tokeniser = valerie_tokeniser_init( );
	server->proxy = 1;
	server->remote_port = DEFAULT_TCP_PORT;
	valerie_tokeniser_parse_new( tokeniser, proxy, ":" );
	strcpy( server->remote_server, valerie_tokeniser_get_string( tokeniser, 0 ) );
	if ( valerie_tokeniser_count( tokeniser ) == 2 )
		server->remote_port = atoi( valerie_tokeniser_get_string( tokeniser, 1 ) );
	valerie_tokeniser_close( tokeniser );
}

/** Wait for a connection.
*/

static int miracle_server_wait_for_connect( miracle_server server )
{
    struct timeval tv;
    fd_set rfds;

    /* Wait for a 1 second. */
    tv.tv_sec = 1;
    tv.tv_usec = 0;

    FD_ZERO( &rfds );
    FD_SET( server->socket, &rfds );

    return select( server->socket + 1, &rfds, NULL, NULL, &tv);
}

/** Run the server thread.
*/

static void *miracle_server_run( void *arg )
{
	miracle_server server = arg;
	pthread_t cmd_parse_info;
	connection_t *tmp = NULL;
	pthread_attr_t thread_attributes;
	socklen_t socksize;

	socksize = sizeof( struct sockaddr );

	miracle_log( LOG_NOTICE, "%s version %s listening on port %i", server->id, VERSION, server->port );

	/* Create the initial thread. We want all threads to be created detached so
	   their resources get freed automatically. (CY: ... hmmph...) */
	pthread_attr_init( &thread_attributes );
	pthread_attr_setdetachstate( &thread_attributes, PTHREAD_CREATE_DETACHED );

	while ( !server->shutdown )
	{
		/* Wait for a new connection. */
		if ( miracle_server_wait_for_connect( server ) )
		{
			/* Create a new block of data to hold a copy of the incoming connection for
			   our server thread. The thread should free this when it terminates. */

			tmp = (connection_t*) malloc( sizeof(connection_t) );
			tmp->owner = &server->parent;
			tmp->parser = server->parser;
			tmp->fd = accept( server->socket, (struct sockaddr*) &(tmp->sin), &socksize );

			/* Pass the connection to a parser thread :-/ */
			if ( tmp->fd != -1 )
				pthread_create( &cmd_parse_info, &thread_attributes, parser_thread, tmp );
		}
	}

	miracle_log( LOG_NOTICE, "%s version %s server terminated.", server->id, VERSION );

	return NULL;
}

/** Execute the server thread.
*/

int miracle_server_execute( miracle_server server )
{
	int error = 0;
	valerie_response response = NULL;
	int index = 0;
	struct sockaddr_in ServerAddr;
	int flag = 1;

	server->shutdown = 0;

	ServerAddr.sin_family = AF_INET;
	ServerAddr.sin_port = htons( server->port );
	ServerAddr.sin_addr.s_addr = INADDR_ANY;
	
	/* Create socket, and bind to port. Listen there. Backlog = 5
	   should be sufficient for listen (). */
	server->socket = socket( AF_INET, SOCK_STREAM, 0 );

	if ( server->socket == -1 )
	{
		server->shutdown = 1;
		perror( "socket" );
		miracle_log( LOG_ERR, "%s unable to create socket.", server->id );
		return -1;
	}

    setsockopt( server->socket, SOL_SOCKET, SO_REUSEADDR, (char *)&flag, sizeof( int ) );

	if ( bind( server->socket, (struct sockaddr *) &ServerAddr, sizeof (ServerAddr) ) != 0 )
	{
		server->shutdown = 1;
		perror( "bind" );
		miracle_log( LOG_ERR, "%s unable to bind to port %d.", server->id, server->port );
		return -1;
	}

	if ( listen( server->socket, 5 ) != 0 )
	{
		server->shutdown = 1;
		perror( "listen" );
		miracle_log( LOG_ERR, "%s unable to listen on port %d.", server->id, server->port );
		return -1;
	}

	fcntl( server->socket, F_SETFL, O_NONBLOCK );

	if ( !server->proxy )
	{
		miracle_log( LOG_NOTICE, "Starting server on %d.", server->port );
		server->parser = miracle_parser_init_local( );
	}
	else
	{
		miracle_log( LOG_NOTICE, "Starting proxy for %s:%d on %d.", server->remote_server, server->remote_port, server->port );
		server->parser = valerie_parser_init_remote( server->remote_server, server->remote_port );
	}

	response = valerie_parser_connect( server->parser );

	if ( response != NULL && valerie_response_get_error_code( response ) == 100 )
	{
		/* read configuration file */
		if ( response != NULL && !server->proxy && server->config != NULL )
		{
			valerie_response_close( response );
			response = valerie_parser_run( server->parser, server->config );

			if ( valerie_response_count( response ) > 1 )
			{
				if ( valerie_response_get_error_code( response ) > 299 )
					miracle_log( LOG_ERR, "Error evaluating server configuration. Processing stopped." );
				for ( index = 0; index < valerie_response_count( response ); index ++ )
					miracle_log( LOG_DEBUG, "%4d: %s", index, valerie_response_get_line( response, index ) );
			}
		}

		if ( response != NULL )
		{
			int result;
			valerie_response_close( response );
			result = pthread_create( &server->thread, NULL, miracle_server_run, server );
			if ( result )
			{
				miracle_log( LOG_CRIT, "Failed to launch TCP listener thread" );
				error = -1;
			}
		}
	}
	else
	{
		miracle_log( LOG_ERR, "Error connecting to parser. Processing stopped." );
		server->shutdown = 1;
		error = -1;
	}

	return error;
}

/** Fetch a units properties
*/

mlt_properties miracle_server_fetch_unit( miracle_server server, int index )
{
	miracle_unit unit = miracle_get_unit( index );
	return unit != NULL ? unit->properties : NULL;
}

/** Shutdown the server.
*/

void miracle_server_shutdown( miracle_server server )
{
	if ( server != NULL && !server->shutdown )
	{
		server->shutdown = 1;
		pthread_join( server->thread, NULL );
		miracle_server_set_config( server, NULL );
		valerie_parser_close( server->parser );
		server->parser = NULL;
		close( server->socket );
	}
}

/** Close the server.
*/

void miracle_server_close( miracle_server server )
{
	if ( server != NULL && mlt_properties_dec_ref( &server->parent ) <= 0 )
	{
		mlt_properties_close( &server->parent );
		miracle_server_shutdown( server );
		free( server );
	}
}
