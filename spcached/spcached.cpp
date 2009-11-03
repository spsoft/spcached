/*
 * Copyright 2007 Stephen Liu
 * For license terms, see the file COPYING along with this library.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#ifndef WIN32

#include "spserver/spserver.hpp"
#include "spserver/splfserver.hpp"

#else

#include "spserver/spiocpserver.hpp"
#include "spserver/spiocplfserver.hpp"

typedef SP_IocpServer SP_Server;
typedef SP_IocpLFServer SP_LFServer;

#endif

#include "spdict/spdictcache.hpp"

#include "spcachemsg.hpp"
#include "spcacheproto.hpp"
#include "spcacheimpl.hpp"
#include "spgetopt.h"

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
	sp_openlog( "spcached", LOG_CONS | LOG_PID | LOG_PERROR, LOG_USER );
#else
	sp_openlog( "spcached", LOG_CONS | LOG_PID, LOG_USER );
#endif

	if( 0 != sp_initsock() ) assert( 0 );

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

	sp_closelog();

	return 0;
}

