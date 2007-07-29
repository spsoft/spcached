/*
 * Copyright 2007 Stephen Liu
 * For license terms, see the file COPYING along with this library.
 */

#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "spbuffer.hpp"
#include "sputils.hpp"
#include "sprequest.hpp"
#include "spresponse.hpp"

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

			char key[ 256 ] = { 0 };
			int flags = 0, bytes = 0;
			time_t expTime = 0;

			if( 0 == strncmp( line, "add ", 4 ) ||
					0 == strncmp( line, "set ", 4 ) ||
					0 == strncmp( line, "replace ", 8 ) ) {
				mMessage->setCommand( 'a' == *line ? "add" : ( 's' == *line ? "set" : "replace" ) );

				int ret = sscanf( line, "%*s %250s %u %ld %d\n", key, &flags, &expTime, &bytes );
				if( 4 == ret && '\0' != key[0] ) {
					mMessage->setExpTime( expTime );
					mMessage->getItem()->setKey( key );

					char buffer[ 512] = { 0 };
					ret = snprintf( buffer, sizeof( buffer ), "VALUE %s %d %d\r\n", key, flags, bytes );
					mMessage->getItem()->appendDataBlock( buffer, ret, bytes + ret + 2 );

					status = eMoreData;
				} else {
					mMessage->setError( "CLIENT_ERROR bad command line format" );
				}
			} else if( 0 == strncmp( line, "get ", 4 ) ) {
				mMessage->setCommand( "get" );

				char * next = strchr( line, ' ' );
				for( ; NULL != next && '\0' != *next; ) {
					char * nextKey = sp_strsep( &next, " " );
					if( NULL != nextKey && '\0' != *nextKey ) {
						mMessage->getKeyList()->append( strdup( nextKey ) );	
					}
				}

			} else if( 0 == strncmp( line, "delete ", 7 ) ) {
				mMessage->setCommand( "delete" );

				sscanf( line, "%*s %250s %ld", key, &expTime );
				if( '\0' != key[0] ) {
					mMessage->setExpTime( expTime );
					mMessage->getItem()->setKey( key );
				} else {
					mMessage->setError( "CLIENT_ERROR bad command line format" );
				}
			} else if( 0 == strncmp( line, "incr ", 5 ) || 0 == strncmp( line, "decr ", 5 ) ) {
				mMessage->setCommand( 'i' == *line ? "incr" : "decr" );

				int ret = sscanf( line, "%*s %250s %u\n", key, &bytes );
				if( 2 == ret && '\0' != key[0] ) {
					mMessage->setDelta( bytes );
					mMessage->getItem()->setKey( key );
				} else {
					mMessage->setError( "CLIENT_ERROR bad command line format" );
				}
			} else {
				mMessage->setCommand( line );
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

		if( eOK == status && item->getDataBytes() > 2 &&0 != strncmp(
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
			if( message->isCommand( "get" ) ) {
				mCacheEx->get( message->getKeyList(), response->getReply()->getFollowBlockList() );
			} else if( message->isCommand( "stats" ) ) {
				mCacheEx->stat( reply );
			} else if( message->isCommand( "version" ) ) {
				reply->append( "VERSION 1.0\r\n" );
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

