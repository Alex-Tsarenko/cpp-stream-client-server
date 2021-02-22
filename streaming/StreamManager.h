#pragma once
#include "Streaming.h"
#include "Tpkt.h"

namespace catapult {

namespace net { class IAsyncTcpSession; }

namespace streaming {


    //
    // IDistributor - interface for Distributor
    //
    class IDistributor
    {
    public:
        virtual ~IDistributor() {}

        virtual void startStreamManager( uint32_t port, uint threadNumber, std::string& errorText ) = 0;
        virtual void stopStreamManager() = 0;
    };

    IDistributor& gStreamManager();

}} // namespace catapult { namespace streaming
