/*
 * Copyright 2007 Stephen Liu
 * For license terms, see the file COPYING along with this library.
 */

#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "spbuffer.hpp"
#include "sputils.hpp"
#include "spmsgblock.hpp"

#include "spcacheimpl.hpp"

#include "spcachemsg.hpp"

class SP_CacheItemMsgBlock : public SP_MsgBlock {
public:
	SP_CacheItemMsgBlock( SP_CacheItem * item );
	virtual ~SP_CacheItemMsgBlock();

	virtual const void * getData() const;
	virtual size_t getSize() const;

private:
	SP_CacheItem * mItem;
};

SP_CacheItemMsgBlock :: SP_CacheItemMsgBlock( SP_CacheItem * item )
{
	mItem = item;
}

SP_CacheItemMsgBlock :: ~SP_CacheItemMsgBlock()
{
	mItem->release();
}

const void * SP_CacheItemMsgBlock :: getData() const
{
	return mItem->getDataBlock();
}

size_t SP_CacheItemMsgBlock :: getSize() const
{
	return mItem->getDataBytes();
}

//---------------------------------------------------------

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
	toDelete->release();
}

void SP_CacheItemHandler :: onHit( const void * item, void * resultHolder )
{
	if( NULL != resultHolder ) {
		SP_MsgBlockList * blockList = (SP_MsgBlockList*)resultHolder;

		SP_CacheItem * it = (SP_CacheItem*)item;
		it->addRef();
		blockList->append( new SP_CacheItemMsgBlock( it ) );
	}
}

//---------------------------------------------------------

SP_CacheEx :: SP_CacheEx( int algo, int maxItems )
{
	mCache = SP_Cache::newInstance( algo, maxItems,
			new SP_CacheItemHandler(), 0 );

	pthread_mutex_init( &mMutex, NULL );
}

SP_CacheEx :: ~SP_CacheEx()
{
	delete mCache;

	pthread_mutex_destroy( &mMutex );
}

int SP_CacheEx :: add( void * item, time_t expTime )
{
	int ret = -1;

	pthread_mutex_lock( &mMutex );

	if( 0 == mCache->get( item, NULL ) ) {
		ret = 0;
		mCache->put( item, expTime );
	}

	pthread_mutex_unlock( &mMutex );

	return ret;
}

int SP_CacheEx :: set( void * item, time_t expTime )
{
	pthread_mutex_lock( &mMutex );

	mCache->put( item, expTime );

	pthread_mutex_unlock( &mMutex );

	return 0;
}

int SP_CacheEx :: replace( void * item, time_t expTime )
{
	int ret = -1;

	pthread_mutex_lock( &mMutex );

	if( mCache->get( item, NULL ) ) {
		ret = 0;
		mCache->put( item, expTime );
	}

	pthread_mutex_unlock( &mMutex );

	return ret;
}

int SP_CacheEx :: erase( const void * key )
{
	int ret = -1;

	pthread_mutex_lock( &mMutex );

	if( mCache->erase( key ) ) ret = 0;

	pthread_mutex_unlock( &mMutex );

	return ret;
}

int SP_CacheEx :: calc( const void * key, int delta, int isIncr, int * newValue )
{
	int ret = -1;

	pthread_mutex_lock( &mMutex );

	time_t expTime = 0;
	SP_CacheItem * oldItem = (SP_CacheItem*)mCache->remove( key, &expTime );
	if( NULL != oldItem ) {
		char * realBlock = strchr( (char*)oldItem->getDataBlock(), '\n' );

		int value = strtol( realBlock ? realBlock + 1 : "", NULL, 10 );

		if( ERANGE == errno ) {
			ret = -2;
			mCache->put( oldItem, expTime );
		} else {
			ret = 0;

			if( isIncr ) {
				value += delta;
			} else {
				value = value > delta ? value - delta : 0;
			}
			* newValue = value;

			char strKey[ 256 ] = { 0 };
			int flags = 0;

			sscanf( (char*)oldItem->getDataBlock(), "%*s %250s %u", strKey, &flags );

			char num[ 32 ] = { 0 };
			snprintf( num, sizeof( num ), "%u", value );

			char buffer[ 512 ] = { 0 };
			snprintf( buffer, sizeof( buffer ), "VALUE %s %d %u\r\n%s\r\n",
					strKey, flags, strlen( num ), num );

			SP_CacheItem * newItem = new SP_CacheItem( strKey );
			newItem->setDataBlock( buffer, strlen( buffer ) );

			mCache->put( newItem, expTime );

			// maybe someone is reading it, so don't delete it, just release it!
			oldItem->release();
		}
	}

	pthread_mutex_unlock( &mMutex );

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

void SP_CacheEx :: get( SP_ArrayList * keyList, SP_MsgBlockList * blockList )
{
	pthread_mutex_lock( &mMutex );

	for( int i = 0; i < keyList->getCount(); i++ ) {
		SP_CacheItem keyItem( (char*)keyList->getItem( i ) );
		mCache->get( &keyItem, blockList );
	}

	pthread_mutex_unlock( &mMutex );

	blockList->append( new SP_SimpleMsgBlock( (void*)"END\r\n", 5, 0 ) );
}

