#pragma once
#include <string>

#include "Tpkt.h"

namespace catapult {
namespace net {

    class ITcpClient
    {
    protected:
        ITcpClient() {}

    public:
        virtual ~ITcpClient() {}

        virtual bool        hasError() = 0;
        virtual std::string errorMessage() = 0;

        virtual bool connect( const std::string& addr, const std::string& port ) = 0;
        virtual void close() = 0;

        virtual bool write( Tpkt& ) = 0;
        virtual bool read( TpktRcv& ) = 0;

//        virtual void startReadLoop( std::function<void(Tpkt&)> ) = 0;
    };

    std::unique_ptr<ITcpClient> createTcpClient();

}} // namespace catapult { namespace streaming
