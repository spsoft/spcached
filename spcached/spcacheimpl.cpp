/*
 * Copyright 2007 Stephen Liu
 * For license terms, see the file COPYING along with this library.
 */

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>
#include <assert.h>

#include "spserver/spbuffer.hpp"
#include "spserver/sputils.hpp"
#include "spserver/spmsgblock.hpp"

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
		Holder_t * holder = (Holder_t*)resultHolder;

		if( 0 == holder->mType ) {
			SP_MsgBlockList * blockList = (SP_MsgBlockList*)holder->mPtr;

			SP_CacheItem * it = (SP_CacheItem*)item;
			it->addRef();
			blockList->append( new SP_CacheItemMsgBlock( it ) );
		} else if( 1 == holder->mType ) {
			SP_CacheItem * it = (SP_CacheItem*)item;
			it->addRef();
			holder->mPtr = it;
		}
	}
}

//---------------------------------------------------------

SP_CacheEx :: SP_CacheEx( int algo, int maxItems )
{
	mCache = SP_DictCache::newInstance( algo, maxItems,
			new SP_CacheItemHandler(), 0 );

	time( &mStartTime );
	mTotalItems = mCmdGet = mCmdSet = 0;

	pthread_mutex_init( &mMutex, NULL );
}

SP_CacheEx :: ~SP_CacheEx()
{
	delete mCache;

	pthread_mutex_destroy( &mMutex );
}

int SP_CacheEx :: add( SP_CacheItem * item, time_t expTime )
{
	int ret = -1;

	pthread_mutex_lock( &mMutex );

	if( 0 == mCache->get( item, NULL ) ) {
		ret = 0;
		mCache->put( item, expTime );
		mTotalItems++;
	}

	pthread_mutex_unlock( &mMutex );

	return ret;
}

int SP_CacheEx :: set( SP_CacheItem * item, time_t expTime )
{
	pthread_mutex_lock( &mMutex );

	mCache->put( item, expTime );
	mTotalItems++;
	mCmdSet++;

	pthread_mutex_unlock( &mMutex );

	return 0;
}

int SP_CacheEx :: replace( SP_CacheItem * item, time_t expTime )
{
	int ret = -1;

	pthread_mutex_lock( &mMutex );

	if( mCache->get( item, NULL ) ) {
		ret = 0;
		mCache->put( item, expTime );
		mTotalItems++;
	}

	pthread_mutex_unlock( &mMutex );

	return ret;
}

int SP_CacheEx :: cas( SP_CacheItem * item, time_t expTime )
{
	int ret = -1;

	SP_CacheItemHandler::Holder_t holder;
	memset( &holder, 0, sizeof( holder ) );
	holder.mType = 1;

	pthread_mutex_lock( &mMutex );

	if( mCache->get( item, &holder ) ) {
		if( NULL != holder.mPtr ) {
			SP_CacheItem * old = (SP_CacheItem*)holder.mPtr;

			if( ( old->getCasUnique() + 1 ) == item->getCasUnique() ) {
				old->release();

				ret = 0;
				mCache->put( item, expTime );
			} else {
				ret = 1;
			}
		}
	}

	pthread_mutex_unlock( &mMutex );

	return ret;
}

int SP_CacheEx :: catbuf( SP_CacheItem * item, time_t expTime, int isAppend )
{
	int ret = -1;

	pthread_mutex_lock( &mMutex );

	time_t oldTime = 0;
	SP_CacheItem * oldItem = (SP_CacheItem*)mCache->remove( item, &oldTime );

	if( NULL != oldItem ) {
		ret = 0;

		SP_CacheItem * newItem = new SP_CacheItem();

		const char * oldPos = strchr( (char*)oldItem->getDataBlock(), '\n' );
		const char * pos = strchr( (char*)item->getDataBlock(), '\n' );

		assert( NULL != oldPos && NULL != pos );

		int oldLen = oldItem->getDataBytes() - ( oldPos + 1 - (char*)oldItem->getDataBlock() ) - 2;
		int len = item->getDataBytes() - ( pos + 1 - (char*)item->getDataBlock() ) - 2;

		int totalBytes = oldLen + len;

		char flags[ 16 ] = { 0 };
		sp_strtok( (char*)item->getDataBlock(), 2, flags, sizeof( flags ) );

		char buffer[ 512] = { 0 };
		int buflen = snprintf( buffer, sizeof( buffer ), "VALUE %s %s %d %llu\r\n",
				item->getKey(), flags, totalBytes, oldItem->getCasUnique() + 1 );
		newItem->appendDataBlock( buffer, buflen, totalBytes + buflen + 2 );

		if( isAppend ) {
			newItem->appendDataBlock( oldPos + 1, oldLen );
			newItem->appendDataBlock( pos + 1, len );
		} else {
			newItem->appendDataBlock( pos + 1, len );
			newItem->appendDataBlock( oldPos + 1, oldLen );
		}

		newItem->appendDataBlock( "\r\n", 2 );
		newItem->setCasUnique( oldItem->getCasUnique() + 1 );
		newItem->setKey( item->getKey() );

		mCache->put( newItem, expTime );

		delete oldItem;
		delete item;
	}

	pthread_mutex_unlock( &mMutex );

	return ret;
}

int SP_CacheEx :: append( SP_CacheItem * item, time_t expTime )
{
	return catbuf( item,expTime, 1 );
}

int SP_CacheEx :: prepend( SP_CacheItem * item, time_t expTime )
{
	return catbuf( item,expTime, 0 );
}

int SP_CacheEx :: erase( const SP_CacheItem * key )
{
	int ret = -1;

	pthread_mutex_lock( &mMutex );

	if( mCache->erase( key ) ) ret = 0;

	pthread_mutex_unlock( &mMutex );

	return ret;
}

int SP_CacheEx :: flushAll( time_t expTime )
{
	return 0;
}

int SP_CacheEx :: calc( const SP_CacheItem * key, int delta, int isIncr, int * newValue )
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
			snprintf( buffer, sizeof( buffer ), "VALUE %s %d %u %llu\r\n%s\r\n",
					strKey, flags, strlen( num ), oldItem->getCasUnique() + 1, num );

			SP_CacheItem * newItem = new SP_CacheItem( strKey );
			newItem->setDataBlock( buffer, strlen( buffer ) );
			newItem->setCasUnique( oldItem->getCasUnique() + 1 );

			mCache->put( newItem, expTime );

			// maybe someone is reading it, so don't delete it, just release it!
			oldItem->release();
		}
	}

	pthread_mutex_unlock( &mMutex );

	return ret;
}

int SP_CacheEx :: incr( const SP_CacheItem * key, int delta, int * newValue )
{
	return calc( key, delta, 1, newValue );
}

int SP_CacheEx :: decr( const SP_CacheItem * key, int delta, int * newValue )
{
	return calc( key, delta, 0, newValue );
}

void SP_CacheEx :: get( SP_ArrayList * keyList, SP_MsgBlockList * blockList )
{
	pthread_mutex_lock( &mMutex );

	SP_CacheItemHandler::Holder_t holder;
	memset( &holder, 0, sizeof( holder ) );
	holder.mPtr = blockList;

	for( int i = 0; i < keyList->getCount(); i++ ) {
		SP_CacheItem keyItem( (char*)keyList->getItem( i ) );
		mCache->get( &keyItem, &holder );
	}
	mCmdGet++;

	pthread_mutex_unlock( &mMutex );

	blockList->append( new SP_SimpleMsgBlock( (void*)"END\r\n", 5, 0 ) );
}

void SP_CacheEx :: stat( SP_Buffer * buffer )
{
	pthread_mutex_lock( &mMutex );

	char temp[ 512 ] = { 0 };

	SP_DictCacheStatistics * stat = mCache->getStatistics();

	snprintf( temp, sizeof( temp ), "STAT pid %u\r\n", getpid() );
	buffer->append( temp );

	snprintf( temp, sizeof( temp ), "STAT uptime %ld\r\n", time( NULL ) - mStartTime );
	buffer->append( temp );

	snprintf( temp, sizeof( temp ), "STAT time %ld\r\n", time( NULL ) );
	buffer->append( temp );

	struct rusage usage;
	getrusage(RUSAGE_SELF, &usage);

	snprintf( temp, sizeof( temp ), "STAT rusage_user %ld.%06ld\r\n",
			usage.ru_utime.tv_sec, usage.ru_utime.tv_usec );
	buffer->append( temp );

	snprintf( temp, sizeof( temp ), "STAT rusage_system %ld.%06ld\r\n",
			usage.ru_stime.tv_sec, usage.ru_stime.tv_usec );
	buffer->append( temp );

	snprintf( temp, sizeof( temp ), "STAT curr_items %d\r\n", stat->getSize() );
	buffer->append( temp );

	snprintf( temp, sizeof( temp ), "STAT total_items %d\r\n", mTotalItems );
	buffer->append( temp );

	snprintf( temp, sizeof( temp ), "STAT cmd_get %d\r\n", mCmdGet );
	buffer->append( temp );

	snprintf( temp, sizeof( temp ), "STAT cmd_set %d\r\n", mCmdSet );
	buffer->append( temp );

	snprintf( temp, sizeof( temp ), "STAT get_hits %d\r\n", stat->getHits() );
	buffer->append( temp );

	snprintf( temp, sizeof( temp ), "STAT get_misses %d\r\n",
			stat->getAccesses() - stat->getHits() );
	buffer->append( temp );

	buffer->append( "END\r\n" );

	delete stat;

	pthread_mutex_unlock( &mMutex );
}

