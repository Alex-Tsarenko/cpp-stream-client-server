#pragma once
#include "Streaming.h"
#include "Tpkt.h"

namespace catapult {
namespace streaming {

namespace net {
    class IAsyncTcpSession;
}

//    struct LiveStreamSessionId
//    {
//        uint32_t m_id;
//    };

//    struct FileStreamSessionId
//    {
//        uint32_t m_id;
//    };

    class IStreamManager
    {
    public:
        virtual ~IStreamManager() {}

        virtual void startStreamManager( uint32_t port, uint threadNumber, std::string& errorText ) = 0;
        virtual void stopStreamManager() = 0;

        //virtual void startStreamingSession( IAsyncTcpSession* session, std::shared_ptr<ServerRequest> request ) = 0;
    };

    IStreamManager& gStreamManager();

}} // namespace catapult { namespace streaming
