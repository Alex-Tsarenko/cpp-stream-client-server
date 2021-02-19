#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <boost/asio.hpp>
#include <boost/lambda/bind.hpp>
#include <boost/lambda/lambda.hpp>


namespace asio = boost::asio;
using     tcp  = boost::asio::ip::tcp;

#include "TcpClient.h"


namespace catapult {
namespace net {

// TcpClient
class TcpClient : public ITcpClient
{
    asio::io_context            m_context;
    tcp::socket                 m_socket;
    asio::deadline_timer        m_deadline;
    boost::posix_time::seconds  m_timeout = boost::posix_time::seconds(60);

    boost::system::error_code m_lastErrorCode;

public:
    TcpClient() : m_context(), m_socket(m_context), m_deadline(m_context)
    {
        // No deadline is required until the first socket operation is started. We
        // set the deadline to positive infinity so that the actor takes no action
        // until a specific deadline is set.
        m_deadline.expires_at(boost::posix_time::pos_infin);

        // Start the persistent actor that checks for deadline expiry.
        check_deadline();
    }

    ~TcpClient()
    {
        LOG( "~TcpClient()" << std::endl );
    }

    void setTimeout( int seconds ) override
    {
        m_timeout = boost::posix_time::seconds(seconds);
    }


    bool hasError() override
    {
        return m_lastErrorCode != boost::system::errc::success;
    }

    std::string errorMessage() override
    {
        return m_lastErrorCode.message();
    }

    bool connect( const std::string& addr, const std::string& port ) override
    {
        // get endpoint
        tcp::resolver::query query( addr, port );
        tcp::resolver::iterator endpoint = tcp::resolver(m_context).resolve(query);

        // start connection
        m_deadline.expires_from_now(m_timeout);
        boost::system::error_code ec = boost::asio::error::would_block;
        boost::asio::async_connect( m_socket, endpoint, boost::lambda::var(ec) = boost::lambda::_1 );

        // perform operation
        do m_context.run_one(); while (ec == boost::asio::error::would_block);

        // check result
        return ( ec || !m_socket.is_open() ) ? false : true;
    }

    void close() override
    {
        boost::system::error_code ec;
        m_socket.close( ec );
    }

    bool write( Tpkt& packet ) override
    {
        packet.updatePacketLenght();
        //LOG( "write: packetLen=" << packet.lenght() << std::endl );

        // start write
        m_deadline.expires_from_now(m_timeout);
        boost::system::error_code ec = boost::asio::error::would_block;
        boost::asio::async_write( m_socket, boost::asio::buffer( packet.ptr(), packet.lenght() ),
                           asio::transfer_exactly( packet.lenght() ),
                           boost::lambda::var(ec) = boost::lambda::_1 );

        // perform operation
        do m_context.run_one(); while (ec == boost::asio::error::would_block);

        m_lastErrorCode = ec;
        return !ec;
    }

    bool read( TpktRcv& packet ) override
    {
        //
        // Read tpkt len
        //
        uint32_t tpktLen = 0;
        boost::system::error_code ec = boost::asio::error::would_block;
        m_deadline.expires_from_now(m_timeout);

        boost::asio::async_read( m_socket,
                           boost::asio::buffer( &tpktLen, sizeof(tpktLen) ),
                           asio::transfer_exactly( sizeof(tpktLen) ),
                           boost::lambda::var(ec) = boost::lambda::_1 );

        // Block until the asynchronous operation has completed.
        do m_context.run_one(); while (ec == boost::asio::error::would_block);

        //LOG( "tpktLen: " << tpktLen << std::endl );
        if ( ec || tpktLen == 0 )
        {
            m_lastErrorCode = ec;
            return false;
        }

        //
        // Read rest tpkt data
        //
        packet.prepareToRead( tpktLen );
        ec = boost::asio::error::would_block;
        m_deadline.expires_from_now(m_timeout);

        boost::asio::async_read( m_socket,
                           boost::asio::buffer( packet.ptr()+4, tpktLen-4 ),
                           asio::transfer_exactly( tpktLen ),
                           boost::lambda::var(ec) = boost::lambda::_1 );

        // Block until the asynchronous operation has completed.
        do m_context.run_one(); while (ec == boost::asio::error::would_block);

        if ( ec != boost::system::errc::success || tpktLen == 0 )
        {
            m_lastErrorCode = ec;
            return false;
        }
        
        return true;
    }

private:
    void check_deadline()
    {
        // Check whether the deadline has passed. We compare the deadline against
        // the current time since a new asynchronous operation may have moved the
        // deadline before this actor had a chance to run.
        if (m_deadline.expires_at() <= asio::deadline_timer::traits_type::now())
        {
            // The deadline has passed. The socket is closed so that any outstanding
            // asynchronous operations are cancelled. This allows the blocked
            // connect(), read_line() or write_line() functions to return.
            boost::system::error_code ignored_ec;
            m_socket.close(ignored_ec);

            // There is no longer an active deadline. The expiry is set to positive
            // infinity so that the actor takes no action until a new deadline is set.
            m_deadline.expires_at(boost::posix_time::pos_infin);
        }

        // Put the actor back to sleep.
        m_deadline.async_wait( std::bind(&TcpClient::check_deadline, this) );
    }
};

std::unique_ptr<ITcpClient> createTcpClient()
{
    return std::unique_ptr<ITcpClient>( new TcpClient() );
}

}}
