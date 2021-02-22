#include "AsyncTcpServer.h"
#include "StreamManager.h"
#include "Tpkt.h"

#include <boost/asio.hpp>
#include <boost/enable_shared_from_this.hpp>

//#include <boost/bind.hpp>
//#include <boost/shared_ptr.hpp>
//#include <boost/cstdint.hpp>

namespace asio = boost::asio;
using     tcp  = boost::asio::ip::tcp;

namespace catapult {
namespace net      {

//
// AsyncTcpSession
//
class AsyncTcpSession : public IAsyncTcpSession
{
    tcp::socket                 m_socket;
    asio::io_context::strand    m_strand;

private:
    TpktLen                     m_packetLen;
    TpktRcv                     m_request;

    boost::system::error_code   m_lastReadError;
    std::optional<std::string>  m_readProtocolError;

    boost::system::error_code   m_lastWriteError;

    bool                        m_received1stRequest = false;

public:
    AsyncTcpSession( asio::io_context& io_context ) : m_socket( io_context ), m_strand( io_context )
    {
        LOG( "TcpSession(" << this << ")" << std::endl );
    }

    virtual ~AsyncTcpSession()
    {
        LOG( "~TcpSession(" << this << ")" << std::endl );
    }

    tcp::socket&  socket() { return m_socket; }

    TpktRcv&    request()               override { return m_request; }
    bool        hasReadError() const    override { return (m_lastReadError || m_readProtocolError.has_value()) ? true : false; }
    bool        hasWriteError() const   override { return (m_lastWriteError) ? true : false; }
    bool        isEof()    const        override { return m_lastReadError == make_error_code(boost::asio::error::eof); }

protected:

    void asyncWrite( Tpkt& response, std::function<void()> func ) override
    {
        response.updatePacketLenght();

        //LOG( "async_write: response.lenght():" << response.lenght() << std::endl );

        auto weak = std::weak_ptr<IAsyncTcpSession>( ((IAsyncTcpSession*)this)->shared_from_this() );
        asio::async_write( m_socket, asio::buffer( response.ptr(), response.lenght() ),
                [=]( boost::system::error_code ec, std::size_t /*bytesTransfered*/ )
        {
            if ( auto shared = weak.lock(); shared )
            {
                m_lastWriteError = ec;
                if ( ec )
                {
                    logSocketError();
                }
                func();
            }
        });
    }

    void asyncRead( std::function<void()> func, uint32_t maxPacketLength ) override
    {
        //LOG( "asyncRead(" << this << ")" << std::endl );

        // Get package lenght
        auto weak = std::weak_ptr<IAsyncTcpSession>( ((IAsyncTcpSession*)this)->shared_from_this() );
        asio::async_read( m_socket, asio::buffer( m_packetLen.bytes, 4 ),
                          asio::transfer_exactly( 4 ),
                          [=]( boost::system::error_code ec, std::size_t bytesTransfered )
        {
            m_received1stRequest = true;

            if ( auto shared = weak.lock(); shared )
            {
                m_lastReadError = ec;
                if ( ec )
                {
                    logSocketError();
                    func();
                    return;
                }

                if ( bytesTransfered != 4 )
                {
                    handleProtocolError("invalid packet size");
                    func();
                    return;
                }

                uint32_t packetLen = m_packetLen.uint32();
                LOG( "asyncRead: packetLen: " << packetLen << std::endl );

                if ( packetLen > maxPacketLength )
                {
                    handleProtocolError( std::string("packet length exceeds ") + std::to_string(maxPacketLength) );
                    func();
                    return;
                }

                if ( packetLen < 8 )
                {
                    handleProtocolError( "invalid packet size (<8)" );
                    func();
                    return;
                }

                // Read package data
                m_request.prepareToRead( packetLen );
                auto weak = std::weak_ptr<IAsyncTcpSession>( ((IAsyncTcpSession*)this)->shared_from_this() );
                asio::async_read( m_socket, asio::buffer( m_request.ptr()+4, packetLen-4 ),
                                  asio::transfer_exactly( packetLen ),
                                  [=]( boost::system::error_code ec, std::size_t bytesTransfered )
                {
                    if ( auto shared = weak.lock(); shared )
                    {
                        //m_request.print("server:");
                        m_lastReadError = ec;
                        if ( ec )
                        {
                            logSocketError();
                        }

                        func();
                    }
                });
            }
        });
    }
    
    void logSocketError()
    {
        if ( isEof() )
        {
            LOG( "AsyncTcpSession: client disconnected (" << m_lastErrorCode.message() << ")" << std::endl );
        }
        else
        {
            LOG( "AsyncTcpSession: socket error: " << m_lastReadError.message() << " " << m_lastReadError.message() << std::endl );
        }
    }

    void closeSession() override
    {
        boost::system::error_code ec;
        m_socket.close(ec);
    }


    void handleProtocolError( std::string errorText )
    {
        m_readProtocolError.emplace( errorText );
    }

    std::string readErrorMessage() const    override
    {
        if ( m_readProtocolError.has_value() )
            return m_readProtocolError.value();

        return m_lastReadError.message();
    }
    std::string writeErrorMessage() const    override
    {

        return m_lastWriteError.message();
    }

    bool received1stRequest() const override
    {
        return m_received1stRequest;
    }


    void postOnStrand( std::function<void()> func ) override
    {
        asio::post( m_strand, func );
    }
};


// AsyncTcpServer
class AsyncTcpServer : public IAsyncTcpServer
{
    boost::asio::io_context         m_context;
    std::unique_ptr<tcp::acceptor>  m_acceptor;

    std::vector<std::thread>        m_threads;

    NewSessionHandler               m_newSessionHandler;
    
    bool                            m_isStopping = false;

public:

    AsyncTcpServer( NewSessionHandler newSessionHandler )
        : m_acceptor(),
          m_newSessionHandler(newSessionHandler)
    {}

    // start
    void start( uint32_t port, uint threadNumber ) override
    {
        m_acceptor = std::unique_ptr<tcp::acceptor>( new tcp::acceptor( m_context, tcp::endpoint( tcp::v4(), port )) );

        startAccept();

        for( uint i=0; i<threadNumber; i++ )
        {
            m_threads.emplace_back( [this] { run(); } );
        }
    }

    void run()
    {
        LOG( "Run started: " << std::this_thread::get_id() << std::endl );
        m_context.run();
        LOG( "Run ended" << std::this_thread::get_id() << std::endl );
    }

    // stop
    void stop() override
    {
        m_isStopping = true;
        m_acceptor->close();
        m_context.stop();

        for( auto& thread: m_threads )
        {
            thread.join();
        }
    }

    // startAccept
    void startAccept()
    {
        std::shared_ptr<IAsyncTcpSession> newSession = std::make_shared<AsyncTcpSession>( m_context );
        
        m_acceptor->async_accept( ((AsyncTcpSession*)newSession.get())->socket(),
                                 [newSession,this] ( const boost::system::error_code& ec )
        {
            if (!ec)
            {
                // handle new session
                m_newSessionHandler( newSession );
            }
            else if ( !m_isStopping )
            {
                LOG_ERR( "async_accept error: " << ec.message() << std::endl );
            }
            startAccept();
        });
    }
};


std::unique_ptr<IAsyncTcpServer> createAsyncTcpServer( NewSessionHandler newSessionHandler )
{
    return std::unique_ptr<IAsyncTcpServer>( new AsyncTcpServer( newSessionHandler ) );
}

}}
