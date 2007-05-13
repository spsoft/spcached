/*
 * Copyright 2007 Stephen Liu
 * For license terms, see the file COPYING along with this library.
 */

#ifndef __spcacheimpl_hpp__
#define __spcacheimpl_hpp__

#include <pthread.h>

#include "spcache.hpp"

class SP_ArrayList;

class SP_CacheItemHandler : public SP_CacheHandler {
public:
	SP_CacheItemHandler();
	virtual ~SP_CacheItemHandler();

	virtual int compare( const void * item1, const void * item2 );
	virtual void destroy( void * item );
	virtual void onHit( const void * item, void * resultHolder );
};

class SP_CacheEx {
public:
	SP_CacheEx( int algo, int maxItems );
	~SP_CacheEx();

	// 0 : STORED, -1 : NOT_STORED
	int add( void * item, time_t expTime );

	// 0 : STORED, -1 : NOT_STORED
	int set( void * item, time_t expTime );

	// 0 : STORED, -1 : NOT_STORED
	int replace( void * item, time_t expTime );

	// 0 : DELETED, -1 : NOT_FOUND
	int erase( const void * key );

	// 0 : OK, -1 : NOT_FOUND, -2 : item is non-numeric value
	int incr( const void * key, int delta, int * newValue );

	// 0 : OK, -1 : NOT_FOUND, -2 : item is non-numeric value
	int decr( const void * key, int delta, int * newValue );

	void get( SP_ArrayList * keyList, SP_Buffer * outBuffer );

private:

	// 0 : OK, -1 : NOT_FOUND, -2 : item is non-numeric value
	int calc( const void * key, int delta, int isIncr, int * newValue );

	SP_Cache * mCache;

	pthread_rwlock_t mLock;
};

#endif

