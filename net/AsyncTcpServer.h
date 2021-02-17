#pragma once
#include <stdlib.h>

#include "Streaming.h"
#include "Tpkt.h"

namespace catapult {
namespace net      {

    class IAsyncTcpSession:  public std::enable_shared_from_this<IAsyncTcpSession>
    {
    public:
        virtual void asyncRead( std::function<void()> ) = 0;
        virtual void asyncWrite( Tpkt&, std::function<void()> ) = 0;
        //virtual void asyncWrite( std::vector<uint8_t>&& response, std::function<void()> func ) = 0;

        virtual TpktRcv&    request() = 0;
        virtual bool        isEof()              const = 0;
        virtual bool        hasReadError()       const = 0;
        virtual std::string readErrorMessage()   const = 0;
        virtual bool        hasWriteError()      const = 0;
        virtual std::string writeErrorMessage()  const = 0;


        virtual void closeSession() = 0;

        virtual ~IAsyncTcpSession() {}
    };

    typedef std::function< void(std::shared_ptr<IAsyncTcpSession>) > NewSessionHandler;

    class IAsyncTcpServer
    {
    public:
        virtual void start( uint32_t port, uint threadNumber ) = 0;
        virtual void stop() = 0;

        virtual ~IAsyncTcpServer() {}
    };

    std::unique_ptr<IAsyncTcpServer> createAsyncTcpServer( NewSessionHandler newSessionHandler );

}} // namespace catapult { namespace streaming
