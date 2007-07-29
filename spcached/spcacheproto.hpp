/*
 * Copyright 2007 Stephen Liu
 * For license terms, see the file COPYING along with this library.
 */

#ifndef __spcacheproto_hpp__
#define __spcacheproto_hpp__

#include "spmsgdecoder.hpp"
#include "sphandler.hpp"

class SP_CacheEx;
class SP_CacheProtoMessage;

class SP_CacheMsgDecoder : public SP_MsgDecoder {
public:
	SP_CacheMsgDecoder();
	virtual ~SP_CacheMsgDecoder();

	virtual int decode( SP_Buffer * inBuffer );
	SP_CacheProtoMessage * getMsg();

private:
	SP_CacheProtoMessage * mMessage;
};

class SP_CacheProtoHandler : public SP_Handler {
public:
	SP_CacheProtoHandler( SP_CacheEx * cacheEx );
	virtual ~SP_CacheProtoHandler();

	virtual int start( SP_Request * request, SP_Response * response );

	// return -1 : terminate session, 0 : continue
	virtual int handle( SP_Request * request, SP_Response * response );

	virtual void error( SP_Response * response );

	virtual void timeout( SP_Response * response );

	virtual void close();

private:
	SP_CacheEx * mCacheEx;
};

class SP_CacheProtoHandlerFactory : public SP_HandlerFactory {
public:
	SP_CacheProtoHandlerFactory( SP_CacheEx * cacheEx );
	virtual ~SP_CacheProtoHandlerFactory();

	virtual SP_Handler * create() const;

private:
	SP_CacheEx * mCacheEx;
};

#endif

