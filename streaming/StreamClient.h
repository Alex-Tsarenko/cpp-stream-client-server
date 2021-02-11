#pragma once
#include "Streaming.h"
#include "StreamingTpkt.h"

namespace catapult {
namespace streaming {


    class IStreamClient
    {
    protected:
        IStreamClient() {};

    public:
        virtual ~IStreamClient() {}

        virtual bool        hasError() = 0;
        virtual std::string errorMessage() = 0;

        virtual bool connect( const std::string& addr, const std::string& port ) = 0;
        virtual void close() = 0;

        virtual bool write( net::Tpkt& ) = 0;
        virtual bool read( net::TpktRcv& ) = 0;

    //        virtual void startReadLoop( std::function<void(Tpkt&)> ) = 0;
    };

    std::unique_ptr<IStreamClient> createStreamingClient();

}} // namespace catapult { namespace streaming
