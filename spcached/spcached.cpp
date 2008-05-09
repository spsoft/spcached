/*
 * Copyright 2007 Stephen Liu
 * For license terms, see the file COPYING along with this library.
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>

#include "spserver.hpp"
#include "splfserver.hpp"

#include "spdictcache.hpp"

#include "spcachemsg.hpp"
#include "spcacheproto.hpp"
#include "spcacheimpl.hpp"

int main( int argc, char * argv[] )
{
	int port = 11216, maxThreads = 1, maxCount = 100000;
	const char * serverType = "hahs";

	extern char *optarg ;
	int c ;

	while( ( c = getopt ( argc, argv, "p:t:c:s:v" )) != EOF ) {
		switch ( c ) {
			case 'p' :
				port = atoi( optarg );
				break;
			case 't':
				maxThreads = atoi( optarg );
				break;
			case 'c':
				maxCount = atoi( optarg );
				break;
			case 's':
				serverType = optarg;
				break;
			case '?' :
			case 'v' :
				printf( "Usage: %s [-p <port>] [-t <threads>] [-c <cache_items>] [-s <hahs|lf>]\n", argv[0] );
				exit( 0 );
		}
	}

#ifdef LOG_PERROR
	openlog( "spcached", LOG_CONS | LOG_PID | LOG_PERROR, LOG_USER );
#else
	openlog( "spcached", LOG_CONS | LOG_PID, LOG_USER );
#endif

	SP_CacheEx cacheEx( SP_DictCache::eFIFO, maxCount > 0 ? maxCount : 100000 );

	if( 0 == strcasecmp( serverType, "hahs" ) ) {
		SP_Server server( "", port, new SP_CacheProtoHandlerFactory( &cacheEx ) );

		server.setTimeout( 60 );
		server.setMaxThreads( maxThreads );
		server.setReqQueueSize( 100, "SERVER_ERROR Server is busy now\r\n" );

		server.runForever();
	} else {
		SP_LFServer server( "", port, new SP_CacheProtoHandlerFactory( &cacheEx ) );

		server.setTimeout( 60 );
		server.setMaxThreads( maxThreads );
		server.setReqQueueSize( 100, "SERVER_ERROR Server is busy now\r\n" );

		server.runForever();
	}

	closelog();

	return 0;
}

