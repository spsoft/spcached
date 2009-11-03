/*
 * Copyright 2007 Stephen Liu
 * For license terms, see the file COPYING along with this library.
 */

#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "spserver/spbuffer.hpp"
#include "spserver/sputils.hpp"
#include "spserver/sprequest.hpp"
#include "spserver/spresponse.hpp"

#include "spcacheproto.hpp"
#include "spcachemsg.hpp"
#include "spcacheimpl.hpp"

static char * sp_strsep(char **s, const char *del)
{
	char *d, *tok;

	if (!s || !*s) return NULL;
	tok = *s;
	d = strstr(tok, del);

	if (d) {
		*s = d + strlen(del);
		*d = '\0';
	} else {
		*s = NULL;
	}

	return tok;
}

SP_CacheMsgDecoder :: SP_CacheMsgDecoder()
{
	mMessage = NULL;
}

SP_CacheMsgDecoder :: ~SP_CacheMsgDecoder()
{
	if( NULL != mMessage ) delete mMessage;
}

int SP_CacheMsgDecoder :: decode( SP_Buffer * inBuffer )
{
	int status = eMoreData;

	if( NULL == mMessage ) {
		char * line = inBuffer->getLine();
		if( NULL != line ) {
			status = eOK;

			mMessage = new SP_CacheProtoMessage();

			char cmd[ 32 ] = { 0 }, key[ 256 ] = { 0 }, exptime[ 16 ] = { 0 }, bytes[ 16 ] = { 0 };

			const char * next = line;
			sp_strtok( next, 0, cmd, sizeof( cmd ), ' ', &next );

			mMessage->setCommand( cmd );

			if( 0 == strcasecmp( cmd, "add" ) || 0 == strcasecmp( cmd, "set" )
					|| 0 == strcasecmp( cmd, "replace" ) || 0 == strcasecmp( cmd, "cas" )
					|| 0 == strcasecmp( cmd, "append" ) || 0 == strcasecmp( cmd, "prepend" ) ) {
				char flags[ 16 ] = { 0 }, casunique[ 32 ] = { 0 };

				if( NULL != next ) sp_strtok( next, 0, key, sizeof( key ), ' ', &next );
				if( NULL != next ) sp_strtok( next, 0, flags, sizeof( flags ), ' ', &next );
				if( NULL != next ) sp_strtok( next, 0, exptime, sizeof( exptime ), ' ', &next );
				if( NULL != next ) sp_strtok( next, 0, bytes, sizeof( bytes ), ' ', &next );
				if( NULL != next ) sp_strtok( next, 0, casunique, sizeof( casunique ) );

				int ret = -1;

				if( '\0' != key[0] && '\0' != bytes[0] ) {
					ret = 0;

					mMessage->setExpTime( strtoul( exptime, NULL, 10 ) );
					mMessage->getItem()->setKey( key );

					uint64_t casid = 0;
					sscanf( casunique, "%llu", &casid );
					casid += 1;

					//strtoull( casunique, NULL, 10 ) + 1;

					char buffer[ 512] = { 0 };
					int len = snprintf( buffer, sizeof( buffer ), "VALUE %s %s %s %llu\r\n",
							key, flags, bytes, casid );
					mMessage->getItem()->appendDataBlock( buffer, len, atoi( bytes ) + len + 2 );

					status = eMoreData;

					if( 0 == strcasecmp( cmd, "cas" ) ) {
						if( '\0' != casunique[0] ) {
							mMessage->getItem()->setCasUnique( casid );
						} else {
							mMessage->setError( "CLIENT_ERROR bad command line format" );
						}
					}
				} else {
					mMessage->setError( "CLIENT_ERROR bad command line format" );
				}
			} else if( 0 == strcasecmp( cmd, "get" ) || 0 == strcasecmp( cmd, "gets" ) ) {
				char * next = strchr( line, ' ' );
				for( ; NULL != next && '\0' != *next; ) {
					char * nextKey = sp_strsep( &next, " " );
					if( NULL != nextKey && '\0' != *nextKey ) {
						mMessage->getKeyList()->append( strdup( nextKey ) );	
					}
				}

			} else if( 0 == strcasecmp( cmd, "delete" ) ) {
				sscanf( line, "%*s %250s %15s", key, exptime );
				if( '\0' != key[0] ) {
					mMessage->setExpTime( strtoul( exptime, NULL, 10 ) );
					mMessage->getItem()->setKey( key );
				} else {
					mMessage->setError( "CLIENT_ERROR bad command line format" );
				}
			} else if( 0 == strcasecmp( cmd, "incr" ) || 0 == strcasecmp( cmd, "decr" ) ) {
				int ret = sscanf( line, "%*s %250s %15s\n", key, bytes );
				if( 2 == ret && '\0' != key[0] ) {
					mMessage->setDelta( atoi( bytes ) );
					mMessage->getItem()->setKey( key );
				} else {
					mMessage->setError( "CLIENT_ERROR bad command line format" );
				}
			} else if( 0 == strcasecmp( cmd, "flush_all" ) ) {
				sp_strtok( line, 1, exptime, sizeof( exptime ) );
				mMessage->setExpTime( strtoul( exptime, NULL, 10 ) );
			}

			free( line );
		}
	}

	if( NULL != mMessage && NULL == mMessage->getError() && inBuffer->getSize() > 0 ) {
		SP_CacheItem * item = mMessage->getItem();
		size_t bytes = item->getBlockCapacity() - item->getDataBytes();
		if( bytes > 0 ) {
			bytes = bytes > inBuffer->getSize() ? inBuffer->getSize() : bytes;
			item->appendDataBlock( inBuffer->getBuffer(), bytes );
			inBuffer->erase( bytes );
		}

		if( item->getBlockCapacity() <= item->getDataBytes() ) status = eOK;

		if( eOK == status && item->getDataBytes() > 2 && 0 != strncmp(
				((char*)item->getDataBlock()) + item->getDataBytes() - 2, "\r\n", 2 ) ) {
			mMessage->setError( "CLIENT_ERROR bad data chunk" );
		}
	}

	return status;
}

SP_CacheProtoMessage * SP_CacheMsgDecoder :: getMsg()
{
	return mMessage;
}

//---------------------------------------------------------

SP_CacheProtoHandler :: SP_CacheProtoHandler( SP_CacheEx * cacheEx )
{
	mCacheEx = cacheEx;
}

SP_CacheProtoHandler :: ~SP_CacheProtoHandler()
{
}

int SP_CacheProtoHandler :: start( SP_Request * request, SP_Response * response )
{
	request->setMsgDecoder( new SP_CacheMsgDecoder() );
	return 0;
}

int SP_CacheProtoHandler :: handle( SP_Request * request, SP_Response * response )
{
	int ret = 0;

	SP_CacheMsgDecoder * decoder = (SP_CacheMsgDecoder*)request->getMsgDecoder();
	SP_CacheProtoMessage * message = (SP_CacheProtoMessage*)decoder->getMsg();
	SP_Buffer * reply = response->getReply()->getMsg();

	if( NULL == message->getError() ) {
		SP_CacheItem * item = message->takeItem();

		if( NULL != item ) {
			if( message->isCommand( "add" ) ) {
				if( 0 == mCacheEx->add( item, message->getExpTime() ) ) {
					reply->append( "STORED\r\n" );
				} else {
					reply->append( "NOT_STORED\r\n" );
					delete item;
				}
			} else if( message->isCommand( "set" ) ) {
				mCacheEx->set( item, message->getExpTime() );
				reply->append( "STORED\r\n" );
			} else if( message->isCommand( "replace" ) ) {
				if( 0 == mCacheEx->replace( item, message->getExpTime() ) ) {
					reply->append( "STORED\r\n" );
				} else {
					reply->append( "NOT_STORED\r\n" );
					delete item;
				}
			} else if( message->isCommand( "cas" ) ) {
				ret = mCacheEx->cas( item, message->getExpTime() );

				if( 0 == ret ) {
					reply->append( "STORED\r\n" );
				} else {
					reply->append( 1 == ret ? "EXISTS\r\n" : "NOT_FOUND\r\n" );
					delete item;
				}

				ret = 0;
			} else if( message->isCommand( "append" ) ) {
				if( 0 == mCacheEx->append( item, message->getExpTime() ) ) {
					reply->append( "STORED\r\n" );
				} else {
					reply->append( "NOT_STORED\r\n" );
					delete item;
				}
			} else if( message->isCommand( "prepend" ) ) {
				if( 0 == mCacheEx->prepend( item, message->getExpTime() ) ) {
					reply->append( "STORED\r\n" );
				} else {
					reply->append( "NOT_STORED\r\n" );
					delete item;
				}
			} else if( message->isCommand( "delete" ) ) {
				if( 0 == mCacheEx->erase( item ) ) {
					reply->append( "DELETED\r\n" );
				} else {
					reply->append( "NOT_FOUND\r\n" );
				}
				delete item;
			} else if( message->isCommand( "incr" )
					|| message->isCommand( "decr" ) ) {

				int ret = 0, newValue = 0;

				if( message->isCommand( "incr" ) ) {
					ret = mCacheEx->incr( item, message->getDelta(), &newValue );
				} else {
					ret = mCacheEx->decr( item, message->getDelta(), &newValue );
				}

				if( 0 == ret ) {
					char buffer[ 32 ] = { 0 };
					snprintf( buffer, sizeof( buffer ), "%u\r\n", newValue );
					reply->append( buffer );
				} else if( -1 == ret ) {
					reply->append( "NOT_FOUND\r\n" );
				} else if( -2 == ret ) {
					reply->append( "CLIENT_ERROR cannot increment or decrement non-numeric value" );
				} else {
					reply->append( "ERROR\r\n" );
				}

				delete item;
			} else {
				ret = 1;
				reply->append( "ERROR unknown command: " );
				reply->append( message->getCommand() );
				reply->append( "\r\n" );
			}
		} else {
			if( message->isCommand( "get" ) || message->isCommand( "gets" ) ) {
				mCacheEx->get( message->getKeyList(), response->getReply()->getFollowBlockList() );
			} else if( message->isCommand( "flush_all" ) ) {
				mCacheEx->flushAll( message->getExpTime() );
				reply->append( "OK\r\n" );
			} else if( message->isCommand( "stats" ) ) {
				mCacheEx->stat( reply );
			} else if( message->isCommand( "version" ) ) {
				reply->append( "VERSION 1.2.5\r\n" );
			} else if( message->isCommand( "quit" ) ) {
				ret = 1;
			} else {
				ret = 1;
				reply->append( "ERROR unknown command: " );
				reply->append( message->getCommand() );
				reply->append( "\r\n" );
			}
		}
	} else {
		reply->append( message->getError() );
		reply->append( "\r\n" );
		ret = 1;
	}

	request->setMsgDecoder( new SP_CacheMsgDecoder() );

	return ret;
}

void SP_CacheProtoHandler :: error( SP_Response * response )
{
}

void SP_CacheProtoHandler :: timeout( SP_Response * response )
{
}

void SP_CacheProtoHandler :: close()
{
}

//---------------------------------------------------------

SP_CacheProtoHandlerFactory :: SP_CacheProtoHandlerFactory( SP_CacheEx * cacheEx )
{
	mCacheEx = cacheEx;
}

SP_CacheProtoHandlerFactory :: ~SP_CacheProtoHandlerFactory()
{
}

SP_Handler * SP_CacheProtoHandlerFactory :: create() const
{
	return new SP_CacheProtoHandler( mCacheEx );
}

