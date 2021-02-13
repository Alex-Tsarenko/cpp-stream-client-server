#include <memory>
#include <thread>
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
class IStreamerSession: public std::enable_shared_from_this<IStreamerSession>
{
public:
    virtual ~IStreamerSession() {}

    virtual void startSession( std::shared_ptr<IAsyncTcpSession> session ) = 0;
    virtual bool isLiveStreamRunning() = 0;
    virtual void addViewer( std::shared_ptr<IAsyncTcpSession> viewerTcpSession ) = 0;
    virtual void removeViewer( std::shared_ptr<ViewerSession> ) = 0;

    virtual void sendErrorResponse( std::string errorText) = 0;

};

//-------------------------------------------------------------------------------------------------------------------------------

//
// ViewerSession
//
class ViewerSession: std::enable_shared_from_this<ViewerSession>
{
    friend class StreamerSession;

    // Tcp session
    std::shared_ptr<IAsyncTcpSession>   m_tcpSession;

    // Back reference to streamer (its owner)
    std::weak_ptr<IStreamerSession>     m_streamerSession;

    StreamingTpkt                       m_response; //?

public:

    ViewerSession( std::shared_ptr<IAsyncTcpSession> tcpSession, std::weak_ptr<IStreamerSession>&& streamerSession )
        : m_tcpSession(tcpSession),
          m_streamerSession( std::move(streamerSession) )
    {
    }

    ~ViewerSession()
    {
        if ( m_tcpSession )
            m_tcpSession->closeSession();
    }

    void readNextClientRequest()
    {
        m_tcpSession->asyncRead( [this]()
        {
            if ( m_tcpSession->hasError() )
            {
                LOG_ERR( "ViewerSession asyncRead error: " << m_tcpSession->errorMessage() << std::endl );

                StreamingTpkt response( 0, cmd::ERROR_STREAMING_RESPONSE, m_tcpSession->errorMessage() );
                if ( auto shared = m_streamerSession.lock(); shared )
                {
                    shared->removeViewer( shared_from_this() );
                }
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
        if ( auto shared = m_streamerSession.lock(); shared->isLiveStreamRunning() )
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
                LOG_ERR( "ViewerSession asyncWrite error: " << m_tcpSession->errorMessage() << std::endl );
                m_tcpSession->closeSession();
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
                LOG_ERR( "sendStreamingData:asyncWrite error: " << m_tcpSession->errorMessage() << std::endl );
                m_tcpSession->closeSession();
            }
        });
    }
};

//-------------------------------------------------------------------------------------------------------------------------------

//
// StreamerSession
//
class StreamerSession : public IStreamerSession
{
    typedef std::shared_ptr<ViewerSession> ViewerSessionPtr;

    friend class StreamManager;
    friend class ViewerSession;

    StreamId                            m_streamId;
    std::shared_ptr<IAsyncTcpSession>   m_tcpSession;
    StreamingTpkt                       m_response;
    
    StreamingTpkt                       m_streamDataResponse;

    std::set<ViewerSessionPtr>          m_viewers;
    std::mutex                          m_viewersMutex;

    EndSessionHandler                   m_endSessionHandler;

public:

    StreamerSession( StreamId& streamId, EndSessionHandler endSessionHandler )
        : m_streamId(streamId),
          m_endSessionHandler(endSessionHandler)
    {
        LOG( "StreamerSession: " << m_streamId.m_id << std::endl );
    }

    ~StreamerSession()
    {
        LOG( "~StreamerSession: " << m_streamId.m_id << std::endl );
        if ( m_tcpSession )
            m_tcpSession->closeSession();
    }
    
    void startSession( std::shared_ptr<IAsyncTcpSession> tcpSession ) override
    {
        m_tcpSession = tcpSession;
        if ( m_tcpSession )
        {
            sendOkStreamingResponse();
        }
        else
        {
            LOG_ERR( "internal error" );
            sendErrorResponse( "StreamerSession::startSession: internal error" );
        }
    }

    bool isLiveStreamRunning() override { return m_tcpSession ? true : false; }

    void addViewer( std::shared_ptr<IAsyncTcpSession> viewerTcpSession ) override
    {
        auto viewerSession = std::make_shared<ViewerSession>( viewerTcpSession, std::weak_ptr<IStreamerSession>( shared_from_this() ) );
        {
            const std::lock_guard<std::mutex> autolock( m_viewersMutex );
            m_viewers.insert( viewerSession );
        }
        viewerSession->sendResponse();
    }

    void removeViewer( std::shared_ptr<ViewerSession> viewerSession ) override
    {
        auto it = m_viewers.find( viewerSession );
        if ( it == m_viewers.end() )
        {
            LOG_ERR( "removeViewer: internal error")
        }
        else
        {
            std::shared_ptr<ViewerSession> shared = *it;
            {
                const std::lock_guard<std::mutex> autolock( m_viewersMutex );
                m_viewers.erase( it );
            }
            // when shared will be deleted it will call closeSession,
            // so it shoul de out of lock
        }
    }

    void sendOkStreamingResponse()
    {
        m_response.init( 0, cmd::OK_STREAMING_RESPONSE );
        m_tcpSession->asyncWrite( m_response, [this]
        {
            if ( m_tcpSession && m_tcpSession->hasError() )
            {
                LOG_ERR( "asyncWrite error: " << m_tcpSession->errorMessage() << std::endl );
                m_tcpSession->closeSession();
                m_tcpSession = nullptr;
                return;
            }
            readNextClientRequest();
        });
    }

    void sendErrorResponse( std::string errorText) override
    {
        m_response.init( 0, cmd::ERROR_STREAMING_RESPONSE, errorText );
        m_tcpSession->asyncWrite( m_response, [this]
        {
            if ( m_tcpSession && m_tcpSession->hasError() )
            {
                LOG_ERR( "asyncWrite error: " << m_tcpSession->errorMessage() << std::endl );
                m_tcpSession->closeSession();
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
            if ( m_tcpSession && m_tcpSession->hasError() )
            {
                if ( !m_tcpSession->isEof() )
                {
                    LOG_ERR( "StreamerSession asyncRead error: " << m_tcpSession->errorMessage() << std::endl );
                }

                //TODO ?sendErrorResponse?
                m_tcpSession->closeSession();
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
    std::shared_ptr<IAsyncTcpServer>                     m_tcpServer;

    std::map<StreamId,std::shared_ptr<IStreamerSession>> m_liveStreamMap;
    std::mutex                                           m_liveStreamMutex;


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
        errorText = "";
    }

    void stopStreamManager() override
    {
        LOG( "m_liveStreamMap.size()=" << m_liveStreamMap.size() << std::endl );
        m_tcpServer->stop();
        LOG( "stopStreamManager ended" << std::endl );
    }

    void handleNewStreamSession( std::shared_ptr<IAsyncTcpSession> newSession )
    {
        //LOG( "handleNewStreamSession( TcpSession:" << newSession.get() << ")" << std::endl );

        newSession->asyncRead( [newSession,this]()
        {
            // handle error
            if ( newSession->hasError() )
            {
                LOG_ERR( "StreamManager asyncRead error: " << newSession->errorMessage() << std::endl );
                newSession->closeSession();
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
                });
            }
        });
    }

    void handleStartStreaming( StreamId& streamId, std::shared_ptr<IAsyncTcpSession> tcpSession )
    {
        m_liveStreamMutex.lock();

        auto stream = m_liveStreamMap.find( streamId );
        if ( stream != m_liveStreamMap.end() )
        {
            m_liveStreamMutex.unlock();

            // Have some viewers connected before?
            if ( !stream->second->isLiveStreamRunning() )
            {
                stream->second->startSession( tcpSession );
                return;
            }

            //TODO send error message "session already running"
            //tcpSession->asyncWrite(...)

            return;
        }

        // Add session
        EndSessionHandler handler = std::bind( &StreamManager::handleEndStreamingSession, this, std::placeholders::_1);
        std::shared_ptr<IStreamerSession> session = std::make_shared<StreamerSession>( streamId, handler );
        m_liveStreamMap[ streamId ] = session;
        m_liveStreamMutex.unlock();

        // Start session
        session->startSession( tcpSession );
    }

    void handleEndStreamingSession( StreamId& streamId )
    {
        std::thread( [=] { m_liveStreamMap.erase( streamId ); } ).detach();
    }

    void handleViewerConnection( StreamId& streamId, std::shared_ptr<IAsyncTcpSession> tcpSession )
    {
        std::shared_ptr<IStreamerSession> session;

        // get streaming session
        {
            const std::lock_guard<std::mutex> autolock( m_liveStreamMutex );

            auto it = m_liveStreamMap.find( streamId );
            if ( it != m_liveStreamMap.end() )
            {
                session = it->second;
            }
            else
            {
                EndSessionHandler handler = std::bind( &StreamManager::handleEndStreamingSession, this, std::placeholders::_1);
                session = std::make_shared<StreamerSession>( streamId, handler );
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
