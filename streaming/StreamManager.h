#pragma once
#include "Streaming.h"
#include "Tpkt.h"

namespace catapult {

namespace net { class IAsyncTcpSession; }

namespace streaming {


    class IDistributer
    {
    public:
        virtual ~IDistributer() {}

        virtual void startStreamManager( uint32_t port, uint threadNumber, std::string& errorText ) = 0;
        virtual void stopStreamManager() = 0;
    };

    IDistributer& gStreamManager();

}} // namespace catapult { namespace streaming
