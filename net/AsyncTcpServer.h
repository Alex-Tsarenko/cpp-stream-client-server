#pragma once
#include <stdlib.h>

#include "Streaming.h"
#include "Tpkt.h"

namespace catapult {
namespace net      {

    class IAsyncTcpSession
    {
    public:
        virtual void asyncRead( std::function<void()> ) = 0;
        virtual void asyncWrite( Tpkt&, std::function<void()> ) = 0;
        //virtual void asyncWrite( std::vector<uint8_t>&& response, std::function<void()> func ) = 0;

        virtual TpktRcv&    request() = 0;
        virtual bool        hasError()      const = 0;
        virtual bool        isEof()         const = 0;
        virtual std::string errorMassage()  const = 0;

        virtual void closeSession() = 0;

        virtual ~IAsyncTcpSession() {}
    };

    typedef std::function< void(IAsyncTcpSession*) > NewSessionHandler;

    class IAsyncTcpServer
    {
    public:
        virtual void start( uint32_t port ) = 0;
        virtual void stop() = 0;

        virtual ~IAsyncTcpServer() {}
    };

    std::unique_ptr<IAsyncTcpServer> createAsyncTcpServer( NewSessionHandler newSessionHandler );

}} // namespace catapult { namespace streaming
