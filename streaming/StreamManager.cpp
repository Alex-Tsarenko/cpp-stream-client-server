#include <memory>
#include <thread>
#include <set>
#include <map>
#include <unordered_set>
#include <queue>
#include <strstream>

#include "StreamManager.h"
#include "AsyncTcpServer.h"
#include "StreamingTpkt.h"

namespace catapult {
namespace streaming {

using namespace catapult::net;

// ViewerSessionPtr
typedef std::function<void(StreamId&)>              EndSessionHandler;

// StreamManager
class Distributer;

// ViewerSession
class Viewer;

// IStreamerSession
class ILiveStream: public std::enable_shared_from_this<ILiveStream>
{
public:
    virtual ~ILiveStream() {}

    virtual void startSession( std::shared_ptr<IAsyncTcpSession> session ) = 0;
    virtual bool isLiveStreamRunning() = 0;
    virtual void addViewer( std::shared_ptr<IAsyncTcpSession> viewerTcpSession ) = 0;
    virtual void removeViewer( std::shared_ptr<Viewer> ) = 0;

    virtual void sendErrorResponse( std::string errorText) = 0;
    
    virtual void prepareToStop() = 0;
};

//-------------------------------------------------------------------------------------------------------------------------------

//
// ViewerSession
//
class Viewer: public std::enable_shared_from_this<Viewer>
{
    friend class LiveStream;

    // Tcp session
    std::shared_ptr<IAsyncTcpSession>   m_tcpSession;

    // Back reference to streamer (its owner)
    std::weak_ptr<ILiveStream>     m_streamerSession;

    StreamingTpkt                       m_response; //?
    
    bool                                m_isStopping = false;

public:

    Viewer( std::shared_ptr<IAsyncTcpSession> tcpSession, std::weak_ptr<ILiveStream> streamerSession )
        : m_tcpSession(tcpSession),
          m_streamerSession( streamerSession )
    {
    }

    ~Viewer()
    {
        m_isStopping = true;
        if ( m_tcpSession )
            m_tcpSession->closeSession();
    }

    void readNextClientRequest()
    {
        m_tcpSession->asyncRead( [this, self=shared_from_this()] ()
        {
            if ( m_tcpSession->hasReadError() && !m_isStopping )
            {
                if ( !m_tcpSession->isEof() )
                {
                    LOG_WARN( "ViewerSession asyncRead error: " << m_tcpSession->readErrorMessage() << std::endl );

                    StreamingTpkt response( 0, cmd::ERROR_STREAMING_RESPONSE, m_tcpSession->readErrorMessage() );
                    m_tcpSession->asyncWrite( response, [] {} );
                }

                if ( auto shared = m_streamerSession.lock(); shared )
                {
                    shared->removeViewer( shared_from_this() );
                }
                return;
            }

            //TODO handle viewer response

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
            if ( m_tcpSession->hasWriteError() && !m_isStopping )
            {
                LOG_WARN( "ViewerSession asyncWrite error: " << m_tcpSession->writeErrorMessage() << std::endl );
                m_tcpSession->closeSession();
                return;
            }
            readNextClientRequest();
        });
    }

    void sendStreamingData( StreamingTpkt& packet )
    {
        if ( m_tcpSession.get() )
        {
            m_tcpSession->asyncWrite( packet, [this]
            {
                if ( m_tcpSession->hasWriteError() && !m_isStopping )
                {
                    LOG_WARN( "sendStreamingData:asyncWrite error: " << m_tcpSession->writeErrorMessage() << std::endl );
                    if ( auto shared = m_streamerSession.lock(); shared )
                    {
                        shared->removeViewer( shared_from_this() );
                    }
                }
            });
        }
    }
    
    void prepareToStop()
    {
        m_isStopping = true;
    }
};

//-------------------------------------------------------------------------------------------------------------------------------

//
// LiveStream
//
class LiveStream : public ILiveStream
{
    typedef std::shared_ptr<Viewer>  ViewerSessionPtr;
    typedef std::shared_ptr<StreamingTpkt>  StreamingTpktPtr;

    friend class Distributer;
    friend class Viewer;

    StreamId                            m_streamId;
    std::shared_ptr<IAsyncTcpSession>   m_tcpSession;
    StreamingTpkt                       m_response;
    
    std::queue<StreamingTpktPtr>        m_streamData;
    std::vector<StreamingTpktPtr>       m_streamDataPool;
    std::mutex                          m_streamDataMutex;

    std::set<ViewerSessionPtr>          m_viewers;
    std::mutex                          m_viewersMutex;

    EndSessionHandler                   m_endSessionHandler;
    
    bool                                m_isStopping = false;

public:

    LiveStream( StreamId& streamId, EndSessionHandler endSessionHandler )
        : m_streamId(streamId),
          m_endSessionHandler(endSessionHandler)
    {
        LOG( "StreamerSession: " << m_streamId.m_id << std::endl );
    }

    ~LiveStream()
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
        std::shared_ptr<Viewer> viewerSession = std::make_shared<Viewer>( viewerTcpSession, std::weak_ptr<ILiveStream>( shared_from_this() ) );
        {
            const std::lock_guard<std::mutex> autolock( m_viewersMutex );
            m_viewers.insert( viewerSession );
        }
        viewerSession->sendResponse();
    }

    void removeViewer( std::shared_ptr<Viewer> viewerSession ) override
    {
        auto it = m_viewers.find( viewerSession );
        if ( it == m_viewers.end() )
        {
            LOG_ERR( "removeViewer: internal error")
        }
        else
        {
            std::shared_ptr<Viewer> shared = *it;
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
            if ( m_tcpSession && m_tcpSession->hasWriteError() )
            {
                LOG_WARN( "asyncWrite error: " << m_tcpSession->writeErrorMessage() << std::endl );
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
            if ( m_tcpSession && m_tcpSession->hasWriteError() )
            {
                LOG_WARN( "asyncWrite error: " << m_tcpSession->writeErrorMessage() << std::endl );
                m_tcpSession->closeSession();
                m_tcpSession = nullptr;
                return;
            }
            readNextClientRequest();
        });
    }

    void readNextClientRequest()
    {
        m_tcpSession->asyncRead( [this, self=shared_from_this()] ()
        {
            if ( m_tcpSession->hasReadError() )
            {
                if ( !m_tcpSession->isEof() && !m_isStopping )
                {
                    LOG_WARN( "StreamerSession asyncRead error: " << m_tcpSession->readErrorMessage() << std::endl );

                    StreamingTpkt response( 0, cmd::ERROR_STREAMING_RESPONSE, m_tcpSession->readErrorMessage() );
                    m_tcpSession->asyncWrite( response, [] {} );
                }

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
                            if ( dataLen<4 )
                            {
                                LOG_WARN( "StreamerSession asyncRead error: dataLen=" << dataLen << std::endl );

                                StreamingTpkt response( 0, cmd::ERROR_STREAMING_RESPONSE, "invalid streaming data lenngth" );
                                m_tcpSession->asyncWrite( response, [] {} );
                            }
                            else
                            {
                                {
                                    const std::lock_guard<std::mutex> autolock( m_streamDataMutex );

//                                    if ( m_streamDataPool.empty() )
//                                    {
                                        m_streamData.push( std::make_shared<StreamingTpkt>() );
                                        //_LOG( "m_streamData.size=" << m_streamData.size() );
//                                    }
//                                    else
//                                    {
//                                        //_LOG( "m_streamDataPool.size=" << m_streamDataPool.size() );
//                                        m_streamData.push( m_streamDataPool.front() );
//                                        m_streamDataPool.pop();
//                                    }

                                    m_streamData.back().get()->initWithStreamingData( 0, cmd::STREAMING_DATA, request.restDataPtr(), dataLen );
                                }

                                sendStreamingDataToViewers();
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
                m_tcpSession->asyncWrite( response, []{} );
            }
        });
    }

    void sendStreamingDataToViewers()
    {
        if ( !m_streamData.empty() )
        {
            m_streamDataMutex.lock();
            auto packet = m_streamData.front();
            m_streamData.pop();
            m_streamDataMutex.unlock();

            
            m_tcpSession->postOnStrand( [ this, shared=shared_from_this(), packet ]
            {
                for( auto it = m_viewers.begin(); it != m_viewers.end(); it++ )
                {
                    (*it)->sendStreamingData( *packet.get() );
                }

                const std::lock_guard<std::mutex> autolock( m_streamDataMutex );
                m_streamDataPool.push_back( packet );
            });
        }
    }
    
    void prepareToStop() override
    {
        for( auto& it : m_viewers )
            it->prepareToStop();
        m_isStopping = true;
    }
};

//-------------------------------------------------------------------------------------------------------------------------------

//
// Distributer
//
class Distributer : protected IDistributer
{
    std::shared_ptr<IAsyncTcpServer>                     m_tcpServer;

    std::map<StreamId,std::shared_ptr<ILiveStream>> m_liveStreamMap;
    std::mutex                                           m_liveStreamMutex;

    bool                                                 m_isStopping = false;

public:
    Distributer()
    {

    }

    virtual ~Distributer() {}

    void startStreamManager( uint32_t port, uint threadNumber, std::string& errorText ) override
    {
        m_tcpServer = createAsyncTcpServer(
            std::bind( &Distributer::handleNewStreamSession, this, std::placeholders::_1 )
        );
        m_tcpServer->start( port, threadNumber );
        errorText = "";
    }

    void stopStreamManager() override
    {
        for( auto it : m_liveStreamMap )
        {
            it.second->prepareToStop();
        }
        LOG( "m_liveStreamMap.size()=" << m_liveStreamMap.size() << std::endl );
        m_isStopping = true;
        m_tcpServer->stop();
        LOG( "stopStreamManager ended" << std::endl );
    }

    void handleNewStreamSession( std::shared_ptr<IAsyncTcpSession> newSession )
    {
        //LOG( "handleNewStreamSession( TcpSession:" << newSession.get() << ")" << std::endl );

        newSession->asyncRead( [newSession,this] ()
        {
            // handle error
            if ( newSession->hasReadError() )
            {
                if ( !m_isStopping )
                {
                    LOG_WARN( "StreamManager asyncRead error: " << newSession->readErrorMessage() << std::endl );
                    StreamingTpkt response( 0, cmd::ERROR_STREAMING_RESPONSE, newSession->readErrorMessage() );
                    newSession->asyncWrite( response, [newSession] { newSession->closeSession(); } );
                }
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

            //TODO  ? for demo: reconnect or send error message "session already running"
            //tcpSession->asyncWrite(...)

            return;
        }

        // Add session
        EndSessionHandler handler = std::bind( &Distributer::handleEndStreamingSession, this, std::placeholders::_1);
        std::shared_ptr<ILiveStream> session = std::make_shared<LiveStream>( streamId, handler );
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
        std::shared_ptr<ILiveStream> session;

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
                EndSessionHandler handler = std::bind( &Distributer::handleEndStreamingSession, this, std::placeholders::_1);
                session = std::make_shared<LiveStream>( streamId, handler );
                m_liveStreamMap[ streamId ] = session;
            }
        }

        session->addViewer( tcpSession );
    }
};


Distributer sStreamManager;

IDistributer& gStreamManager()
{
    return  (IDistributer&)sStreamManager;
}


}}
