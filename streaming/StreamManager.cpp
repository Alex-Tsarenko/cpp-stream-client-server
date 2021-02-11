#include <memory>
#include <set>
#include <map>
#include <unordered_set>
#include <strstream>

#include "StreamManager.h"
#include "AsyncTcpServer.h"
#include "StreamingTpkt.h"

namespace catapult {
namespace streaming {

using namespace catapult::net;

// ViewerSessionPtr
typedef std::function<void(StreamId&)> EndSessionHandler;

// StreamManager
class StreamManager;

// ViewerSession
class ViewerSession;

// IStreamerSession
class IStreamerSession
{
public:
    virtual ~IStreamerSession() {}

    virtual bool isLiveStreamRunning() = 0;
    virtual void removeViewerSession( ViewerSession* ) = 0;
};

//-------------------------------------------------------------------------------------------------------------------------------

//
// ViewerSession
//
class ViewerSession: std::enable_shared_from_this<ViewerSession>
{
    friend class StreamerSession;

    // Tcp session
    IAsyncTcpSession*  m_tcpSession;

    // Back reference to streamer (its owner)
    IStreamerSession&  m_streamerSession;
    StreamingTpkt      m_response;

public:

    ViewerSession( IAsyncTcpSession* tcpSession, IStreamerSession& streamerSession )
        : m_tcpSession(tcpSession),
          m_streamerSession(streamerSession)
    {
    }

    ~ViewerSession()
    {
        delete  m_tcpSession;
    }

    void readNextClientRequest()
    {
        m_tcpSession->asyncRead( [this]()
        {
            if ( m_tcpSession->hasError() )
            {
                LOG_ERR( "ViewerSession asyncRead error: " << m_tcpSession->errorMassage() << std::endl );

                StreamingTpkt response( 0, cmd::ERROR_STREAMING_RESPONSE, m_tcpSession->errorMassage() );
                m_tcpSession->closeSession();
                delete m_tcpSession;
                m_tcpSession = nullptr;
                //TODO remove(this);
                return;
            }
            //TODO

//            try {

//            }  catch () {
//                StreamingTpkt response( 0, cmd::ERROR_STREAMING_RESPONSE, error.what() );
//                m_tcpSession->asyncWrite( response,
//                []
//                {
////                    this->closeSession();
////                    delete ;
//                });

//            }
        });
    }

    // ViewerSession::sendResponse
    void sendResponse()
    {
        if ( m_streamerSession.isLiveStreamRunning() )
        {
            m_response.init( 0, cmd::OK_STREAMING_RESPONSE );
        }
        else
        {
            m_response.init( 0, cmd::IS_NOT_STARTED_RESPONSE );
        }
         
        m_tcpSession->asyncWrite( m_response, [this]
        {
            if ( m_tcpSession->hasError() )
            {
                LOG_ERR( "ViewerSession asyncWrite error: " << m_tcpSession->errorMassage() << std::endl );
                m_tcpSession->closeSession();
                delete m_tcpSession;
                m_tcpSession = nullptr;
                return;
            }
            readNextClientRequest();
        });
    }

    void sendStreamingData( StreamingTpkt& packet )
    {
        m_tcpSession->asyncWrite( packet, [this]
        {
            if ( m_tcpSession->hasError() )
            {
                LOG_ERR( "sendStreamingData:asyncWrite error: " << m_tcpSession->errorMassage() << std::endl );
                m_tcpSession->closeSession();
                delete m_tcpSession;
                m_tcpSession = nullptr;
                return;
            }
        });
    }
};

//-------------------------------------------------------------------------------------------------------------------------------

//
// StreamerSession
//
class StreamerSession : public IStreamerSession, std::enable_shared_from_this<StreamerSession>
{
    typedef std::shared_ptr<ViewerSession> ViewerSessionPtr;

    friend class StreamManager;
    friend class ViewerSession;

    StreamId                        m_streamId;
    IAsyncTcpSession*               m_tcpSession;
    StreamingTpkt                   m_response;
    
    StreamingTpkt                   m_streamDataResponse;

    std::set<ViewerSessionPtr>      m_viewers;
    std::mutex                      m_mutex;

    EndSessionHandler               m_endSessionHandler;

public:

    StreamerSession( StreamId& streamId, IAsyncTcpSession* session, EndSessionHandler endSessionHandler )
        : m_streamId(streamId),
          m_tcpSession(session),
          m_endSessionHandler(endSessionHandler)
    {
    }

    ~StreamerSession()
    {
        delete m_tcpSession;
    }

    bool isLiveStreamRunning() override { return m_tcpSession != nullptr; }

    void addViewer( IAsyncTcpSession* viewerTcpSession )
    {
        const std::lock_guard<std::mutex> autolock( m_mutex );
        auto viewerSession = std::make_shared<ViewerSession>( viewerTcpSession, *this );
        m_viewers.insert( viewerSession );
        viewerSession->sendResponse();
    }

    void removeViewerSession( ViewerSession* viewerSession ) override
    {
        viewerSession->m_tcpSession->closeSession();
        m_viewers.erase( viewerSession->shared_from_this() );
    }

    void sendOkStreamingResponse()
    {
        m_response.init( 0, cmd::OK_STREAMING_RESPONSE );
        m_tcpSession->asyncWrite( m_response, [this]
        {
            if ( m_tcpSession->hasError() )
            {
                LOG_ERR( "asyncWrite error: " << m_tcpSession->errorMassage() << std::endl );
                m_tcpSession->closeSession();
                delete m_tcpSession;
                m_tcpSession = nullptr;
                return;
            }
            readNextClientRequest();
        });
    }

    void sendErrorResponse( std::string errorText)
    {
        m_response.init( 0, cmd::ERROR_STREAMING_RESPONSE, errorText );
        m_tcpSession->asyncWrite( m_response, [this]
        {
            if ( m_tcpSession->hasError() )
            {
                LOG_ERR( "asyncWrite error: " << m_tcpSession->errorMassage() << std::endl );
                m_tcpSession->closeSession();
                delete m_tcpSession;
                m_tcpSession = nullptr;
                return;
            }
            readNextClientRequest();
        });
    }

    void readNextClientRequest()
    {
        m_tcpSession->asyncRead( [this]()
        {
            if ( m_tcpSession->hasError() )
            {
                if ( !m_tcpSession->isEof() )
                {
                    LOG_ERR( "StreamerSession asyncRead error: " << m_tcpSession->errorMassage() << std::endl );
                }

                //TODO ?sendErrorResponse?
                m_tcpSession->closeSession();
                delete m_tcpSession;
                m_tcpSession = nullptr;
                return;
            }

            try
            {
                StreamingTpktRcv& request = static_cast<StreamingTpktRcv&>( m_tcpSession->request() );

                // version
                uint32_t version;
                request.read( version );
                if ( version != PROTOCOL_VERSION )
                    throw std::runtime_error("invalid protocol version");

                // requestId
                uint32_t requestId;
                request.read( requestId );

                LOG( "StreamerSession: clientRequest:" << cmd::name(requestId) << std::endl );

                switch( requestId )
                {
                    case cmd::END_STREAMING:
                        m_endSessionHandler( m_streamId );
                        break;

                    case cmd::RESTORE_STREAMING:
                        sendErrorResponse( "command RESTORE_STREAMING is not ready" );
                        readNextClientRequest();
                        break;

                    case cmd::STREAMING_DATA:
                    {
                        if ( m_viewers.size() > 0 )
                        {
                            uint32_t dataLen = request.restDataLen();
                            if ( dataLen>4 )
                            {
                                m_streamDataResponse.initWithStreamingData( 0, cmd::STREAMING_DATA, request.restDataPtr()+4, dataLen-4 );
                                sendStreamingDataToViewers();
                            }
                            else
                            {
                                //TODO send error message
                            }
                        }
                        sendOkStreamingResponse();
                        break;
                    }

                    default:
                        auto errText = (std::strstream() << "unsupported command: " << cmd::name(requestId)).str();
                        LOG_ERR( errText << std::endl );
                        throw std::runtime_error( errText );
                }
            }
            catch ( std::runtime_error error )
            {
                LOG_ERR( ": error:" << error.what() << std::endl );
                StreamingTpkt response( 0, cmd::ERROR_STREAMING_RESPONSE, error.what() );
                m_tcpSession->asyncWrite( response,
                []
                {
//                    this->closeSession();
//                    delete ;
                });
            }
        });
    }

    void sendStreamingDataToViewers()
    {
        for( auto it = m_viewers.begin(); it != m_viewers.end(); it++ )
        {
            (*it)->sendStreamingData( m_streamDataResponse );
        }
    }
};

//-------------------------------------------------------------------------------------------------------------------------------

//
// StreamManager
//
class StreamManager : protected IStreamManager
{
    std::unique_ptr<IAsyncTcpServer>                    m_tcpServer;

    std::map<StreamId,std::shared_ptr<StreamerSession>> m_liveStreamMap;
    std::mutex                                          m_mapMutex;


public:
    StreamManager()
    {

    }

    virtual ~StreamManager() {}

    void startStreamManager( uint32_t port, std::string& errorText ) override
    {
        m_tcpServer = createAsyncTcpServer(
            std::bind( &StreamManager::handleNewStreamSession, this, std::placeholders::_1 )
        );
        m_tcpServer->start( port );
    }

    void stopStreamManager() override
    {
        m_tcpServer->stop();
    }

    void handleNewStreamSession( IAsyncTcpSession* newSession )
    {
        newSession->asyncRead( [=]()
        {
            // handle error
            if ( newSession->hasError() )
            {
                LOG_ERR( "StreamManager asyncRead error: " << newSession->errorMassage() << std::endl );
                newSession->closeSession();
                delete newSession;
                return;
            }

            try
            {
                StreamingTpktRcv& request = static_cast<StreamingTpktRcv&>( newSession->request() );

                // version
                uint32_t version;
                request.read( version );
                if ( version != PROTOCOL_VERSION )
                    throw std::runtime_error("invalid protocol version");

                // requestId
                uint32_t requestId;
                request.read( requestId );
                
                LOG( "StreamManager: clientRequest:" << cmd::name(requestId) << std::endl );

                switch( requestId )
                {
                    case cmd::START_STREAMING:
                    {
                        StreamId streamId;
                        request.read( streamId );
                        handleStartStreaming( streamId, newSession );
                        break;
                    }
                    case cmd::START_LIFE_STREAM_VIEWING:
                    {
                        StreamId streamId;
                        request.read( streamId );
                        handleViewerConnection( streamId, newSession );
                        break;
                    }
                    case cmd::START_FILE_STREAM_VIEWING:
                    {
                        // IS NOT READY
                        std::string errorText("START_FILE_STREAM_VIEWING is not ready");
                        StreamingTpkt response( 0, cmd::ERROR_STREAMING_RESPONSE, errorText );
                        newSession->asyncWrite( response, [newSession]
                        {
                            newSession->closeSession();
                            delete newSession;
                            return;
                        });
                        break;
                    }
                    default:
                        auto errText = (std::strstream() << "unsupported command: " << cmd::name(requestId)).str();
                        LOG_ERR( errText << std::endl );
                        throw std::runtime_error( errText );
                }

                LOG( "clientRequest:" << std::endl );
            }
            catch ( std::runtime_error error )
            {
                LOG_ERR( ": error:" << error.what() << std::endl );
                StreamingTpkt response( 0, cmd::ERROR_STREAMING_RESPONSE, error.what() );
                newSession->asyncWrite( response,
                [newSession]
                {
                    newSession->closeSession();
                    delete newSession;
                });
            }
        });
    }

    void handleStartStreaming( StreamId& streamId, IAsyncTcpSession* tcpSession )
    {
        const std::lock_guard<std::mutex> autolock( m_mapMutex );

        auto stream = m_liveStreamMap.find( streamId );
        if ( stream != m_liveStreamMap.end() )
        {
            // Have some viewers connected before?
            if ( stream->second->m_tcpSession == nullptr )
            {
                stream->second->m_tcpSession = tcpSession;
                stream->second->sendOkStreamingResponse();
                return;
            }
            //TODO restore connection to started session
            // ? delete tcpSession
            return;
        }

        // Start/add session
        EndSessionHandler handler = std::bind( &StreamManager::handleEndStreamingSession, this, std::placeholders::_1);
        auto session = std::make_shared<StreamerSession>( streamId, tcpSession, handler );
        m_liveStreamMap[ streamId ] = session;
        session->sendOkStreamingResponse();
    }

    void handleEndStreamingSession( StreamId& streamId )
    {
        auto& session = m_liveStreamMap[streamId];

        if ( session->m_tcpSession != nullptr )
            session->m_tcpSession->closeSession();
        delete session->m_tcpSession;

        m_liveStreamMap.erase( streamId );

    }

    void handleViewerConnection( StreamId& streamId, IAsyncTcpSession* tcpSession )
    {
        std::shared_ptr<StreamerSession> session;

        // get streaming session
        {
            const std::lock_guard<std::mutex> autolock( m_mapMutex );

            auto it = m_liveStreamMap.find( streamId );
            if ( it != m_liveStreamMap.end() )
            {
                session = it->second;
            }
            else
            {
                EndSessionHandler handler = std::bind( &StreamManager::handleEndStreamingSession, this, std::placeholders::_1);
                session = std::make_shared<StreamerSession>( streamId, nullptr, handler );
                m_liveStreamMap[ streamId ] = session;
            }
        }

        session->addViewer( tcpSession );
    }

};


StreamManager sStreamManager;

IStreamManager& gStreamManager()
{
    return  (IStreamManager&)sStreamManager;
}


}}
