/*
 * Copyright 2007 Stephen Liu
 * For license terms, see the file COPYING along with this library.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "spcachemsg.hpp"
#include "spserver/spbuffer.hpp"
#include "spserver/sputils.hpp"

SP_CacheItem :: SP_CacheItem( const char * key )
{
	init();
	mKey = strdup( key );
}

SP_CacheItem :: SP_CacheItem()
{
	init();
}

void SP_CacheItem :: init()
{
	mKey = NULL;

	mDataBlock = NULL;
	mDataBytes = mBlockCapacity = 0;

	mRefCount = 1;

	pthread_mutex_init( &mMutex, NULL );
}

SP_CacheItem :: ~SP_CacheItem()
{
	if( NULL != mKey ) free( mKey );
	mKey = NULL;

	if( NULL != mDataBlock ) free( mDataBlock );
	mDataBlock = NULL;

	pthread_mutex_destroy( &mMutex );
}

void SP_CacheItem :: addRef()
{
	pthread_mutex_lock( &mMutex );
	mRefCount++;
	pthread_mutex_unlock( &mMutex );
}

void SP_CacheItem :: release()
{
	int refCount = 1;

	pthread_mutex_lock( &mMutex );
	mRefCount--;
	refCount = mRefCount;
	pthread_mutex_unlock( &mMutex );

	if( refCount <= 0 ) delete this;
}

void SP_CacheItem :: setKey( const char * key )
{
	char * temp = mKey;
	mKey = strdup( key );

	if( NULL != temp ) free( temp );
}

const char * SP_CacheItem :: getKey() const
{
	return mKey;
}

void SP_CacheItem :: appendDataBlock( const void * dataBlock, size_t dataBytes,
	size_t blockCapacity )
{
	size_t realBytes = mDataBytes + dataBytes;
	realBytes = realBytes > blockCapacity ? realBytes : blockCapacity;

	if( realBytes > mBlockCapacity ) {
		if( NULL == mDataBlock ) {
			mDataBlock = malloc( realBytes + 1 );
		} else {
			mDataBlock = realloc( mDataBlock, realBytes + 1 );
		}
		mBlockCapacity = realBytes;
	}

	memcpy( ((char*)mDataBlock) + mDataBytes, dataBlock, dataBytes );
	((char*)mDataBlock)[ realBytes ] = '\0';

	mDataBytes += dataBytes;
}

void SP_CacheItem :: setDataBlock( const void * dataBlock, size_t dataBytes )
{
	mDataBytes = 0;
	appendDataBlock( dataBlock, dataBytes );
}

const void * SP_CacheItem :: getDataBlock() const
{
	return mDataBlock;
}

size_t SP_CacheItem :: getDataBytes() const
{
	return mDataBytes;
}

size_t SP_CacheItem :: getBlockCapacity() const
{
	return mBlockCapacity;
}

//---------------------------------------------------------

SP_CacheProtoMessage :: SP_CacheProtoMessage()
{
	memset( mCommand, 0, sizeof( mCommand ) );
	mDelta = 0;
	mExpTime = 0;
	mItem = NULL;
	memset( mError, 0, sizeof( mError ) );

	mKeyList = new SP_ArrayList();
}

SP_CacheProtoMessage :: ~SP_CacheProtoMessage()
{
	if( NULL != mItem ) delete mItem;
	mItem = NULL;

	for( ; mKeyList->getCount() > 0; ) {
		free( mKeyList->takeItem( SP_ArrayList::LAST_INDEX ) );
	}
	delete mKeyList;
	mKeyList = NULL;
}

void SP_CacheProtoMessage :: setCommand( const char * command )
{
	snprintf( mCommand, sizeof( mCommand ), "%s", command );
}

const char * SP_CacheProtoMessage :: getCommand() const
{
	return mCommand;
}

int SP_CacheProtoMessage :: isCommand( const char * command ) const
{
	return 0 == strcmp( mCommand, command );
}

void SP_CacheProtoMessage :: setExpTime( time_t expTime )
{
	if( expTime <= 60*60*24*30 && expTime > 0 ) expTime = time( NULL ) + expTime;

	mExpTime = expTime;
}

time_t SP_CacheProtoMessage :: getExpTime() const
{
	return mExpTime;
}

SP_ArrayList * SP_CacheProtoMessage :: getKeyList() const
{
	return mKeyList;
}

void SP_CacheProtoMessage :: setDelta( int delta )
{
	mDelta = delta;
}

int SP_CacheProtoMessage :: getDelta() const
{
	return mDelta;
}

SP_CacheItem * SP_CacheProtoMessage :: getItem()
{
	if( NULL == mItem ) mItem = new SP_CacheItem();

	return mItem;
}

SP_CacheItem * SP_CacheProtoMessage :: takeItem()
{
	SP_CacheItem * temp = mItem;

	mItem = NULL;

	return temp;
}

void SP_CacheProtoMessage :: setError( const char * error )
{
	snprintf( mError, sizeof( mError ), "%s", error );
}

const char * SP_CacheProtoMessage :: getError() const
{
	return '\0' == mError[0] ? NULL : mError;
}

