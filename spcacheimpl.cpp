/*
 * Copyright 2007 Stephen Liu
 * For license terms, see the file COPYING along with this library.
 */

#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "spbuffer.hpp"
#include "sputils.hpp"

#include "spcacheimpl.hpp"

#include "spcachemsg.hpp"

SP_CacheItemHandler :: SP_CacheItemHandler()
{
}

SP_CacheItemHandler :: ~SP_CacheItemHandler()
{
}

int SP_CacheItemHandler :: compare( const void * item1, const void * item2 )
{
	SP_CacheItem * i1 = (SP_CacheItem*)item1, * i2 = (SP_CacheItem*)item2;

	return strcmp( i1->getKey(), i2->getKey() );
}

void SP_CacheItemHandler :: destroy( void * item )
{
	SP_CacheItem * toDelete = (SP_CacheItem*)item;
	delete toDelete;
}

void SP_CacheItemHandler :: onHit( const void * item, void * resultHolder )
{
	/**
	 * Each item sent by the server looks like this:
	 * VALUE <key> <flags> <bytes>\r\n
	 * <data block>\r\n
	 */

	if( NULL != resultHolder ) {
		SP_CacheItem * it = (SP_CacheItem*)item;

		SP_Buffer * outBuffer = (SP_Buffer*)resultHolder;

		char buffer[ 512 ] = { 0 };
		snprintf( buffer, sizeof( buffer ), "VALUE %s %d %d\r\n", it->getKey(),
			it->getFlags(), it->getDataBytes() - 2 );

		outBuffer->append( buffer );
		outBuffer->append( it->getDataBlock(), it->getDataBytes() );
	}
}

//---------------------------------------------------------

SP_CacheEx :: SP_CacheEx( int algo, int maxItems )
{
	mCache = SP_Cache::newInstance( algo, maxItems,
			new SP_CacheItemHandler(), 0 );

	pthread_rwlock_init( &mLock, NULL );
}

SP_CacheEx :: ~SP_CacheEx()
{
	delete mCache;

	pthread_rwlock_destroy( &mLock );
}

int SP_CacheEx :: add( void * item, time_t expTime )
{
	int ret = -1;

	pthread_rwlock_wrlock( &mLock );

	if( 0 == mCache->get( item, NULL ) ) {
		ret = 0;
		mCache->put( item, expTime );
	}

	pthread_rwlock_unlock( &mLock );

	return ret;
}

int SP_CacheEx :: set( void * item, time_t expTime )
{
	pthread_rwlock_wrlock( &mLock );

	mCache->put( item, expTime );

	pthread_rwlock_unlock( &mLock );

	return 0;
}

int SP_CacheEx :: replace( void * item, time_t expTime )
{
	int ret = -1;

	pthread_rwlock_wrlock( &mLock );

	if( mCache->get( item, NULL ) ) {
		ret = 0;
		mCache->put( item, expTime );
	}

	pthread_rwlock_unlock( &mLock );

	return ret;
}

int SP_CacheEx :: erase( const void * key )
{
	int ret = -1;

	pthread_rwlock_wrlock( &mLock );

	if( mCache->erase( key ) ) ret = 0;

	pthread_rwlock_unlock( &mLock );

	return ret;
}

int SP_CacheEx :: calc( const void * key, int delta, int isIncr, int * newValue )
{
	int ret = -1;

	pthread_rwlock_wrlock( &mLock );

	time_t expTime = 0;
	SP_CacheItem * oldItem = (SP_CacheItem*)mCache->remove( key, &expTime );
	if( NULL != oldItem ) {
		int value = strtol( (char*)oldItem->getDataBlock(), NULL, 10 );

		if( ERANGE == errno ) {
			ret = -2;
		} else {
			ret = 0;

			if( isIncr ) {
				value += delta;
			} else {
				value = value > delta ? value - delta : 0;
			}
			* newValue = value;

			char buffer[ 32 ] = { 0 };
			snprintf( buffer, sizeof( buffer ), "%u\r\n", value );
			oldItem->setDataBlock( buffer, strlen( buffer ) );
		}
		mCache->put( oldItem, expTime );
	}

	pthread_rwlock_unlock( &mLock );

	return ret;
}

int SP_CacheEx :: incr( const void * key, int delta, int * newValue )
{
	return calc( key, delta, 1, newValue );
}

int SP_CacheEx :: decr( const void * key, int delta, int * newValue )
{
	return calc( key, delta, 0, newValue );
}

void SP_CacheEx :: get( SP_ArrayList * keyList, SP_Buffer * outBuffer )
{
	pthread_rwlock_rdlock( &mLock );

	for( int i = 0; i < keyList->getCount(); i++ ) {
		SP_CacheItem keyItem( (char*)keyList->getItem( i ) );
		mCache->get( &keyItem, outBuffer );
	}
	outBuffer->append( "END\r\n" );

	pthread_rwlock_unlock( &mLock );
}

