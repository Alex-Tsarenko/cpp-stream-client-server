#pragma once
#include <stdlib.h>

#include "Streaming.h"
#include "Tpkt.h"

namespace catapult {
namespace net      {

    //
    // IAsyncTcpSession - interface for AsyncTcpSession
    //
    class IAsyncTcpSession:  public std::enable_shared_from_this<IAsyncTcpSession>
    {
    public:

        //
        // asyncRead - reads request, that will be available by calling 'request()'
        //
        // 'func' will be called after completed reading
        // 'maxPacketLength' is used to prevent malicious attacks
        //
        virtual void asyncRead( std::function<void()> func, uint32_t maxPacketLength = 10*1024*1024 ) = 0;

        //
        // asyncWrite - writes response
        //
        // 'func' will be called after the write is completed
        //
        virtual void asyncWrite( Tpkt&, std::function<void()> func ) = 0;

        virtual TpktRcv&    request() = 0;
        virtual bool        isEof()              const = 0;
        virtual bool        hasReadError()       const = 0;
        virtual std::string readErrorMessage()   const = 0;
        virtual bool        hasWriteError()      const = 0;
        virtual std::string writeErrorMessage()  const = 0;

        virtual bool        received1stRequest() const = 0;

        virtual void postOnStrand( std::function<void()> func ) = 0;

        virtual void closeSession() = 0;

        virtual ~IAsyncTcpSession() {}
    };

    typedef std::function< void(std::shared_ptr<IAsyncTcpSession>) > NewSessionHandler;

    //
    // IAsyncTcpServer - interface for AsyncTcpServer
    //
    class IAsyncTcpServer
    {
    public:
        virtual void start( uint32_t port, uint threadNumber ) = 0;
        virtual void stop() = 0;

        virtual ~IAsyncTcpServer() {}
    };

    std::unique_ptr<IAsyncTcpServer> createAsyncTcpServer( NewSessionHandler newSessionHandler );

}} // namespace catapult { namespace streaming
