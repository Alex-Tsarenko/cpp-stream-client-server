#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <boost/asio.hpp>

namespace asio = boost::asio;
using     tcp  = boost::asio::ip::tcp;

#include "TcpClient.h"


namespace catapult {
namespace net {

// TcpClient
class TcpClient : public ITcpClient
{
    asio::io_context    m_context;
    tcp::socket         m_socket;

    boost::system::error_code m_lastErrorCode;

public:
    TcpClient() :m_context(), m_socket(m_context)
    {
    }
    ~TcpClient()
    {
        LOG( "~TcpClient()" << std::endl );
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
        tcp::resolver resolver(m_context);
        boost::system::error_code ec;
        boost::asio::connect( m_socket, resolver.resolve(addr, port), ec );

        if ( ec == boost::system::errc::success )
            return true;

        m_lastErrorCode = ec;
        return false;
    }

    void close() override
    {
        m_socket.close();
    }

    bool write( Tpkt& packet ) override
    {
        packet.updatePacketLenght();
        boost::system::error_code ec;
        //LOG( "write: packetLen=" << packet.lenght() << std::endl );
        boost::asio::write( m_socket, boost::asio::buffer( packet.ptr(), packet.lenght() ),
                           asio::transfer_exactly( packet.lenght() ),
                           ec );

        m_lastErrorCode = ec;
        return !ec;
    }

    bool read( TpktRcv& packet ) override
    {
        //
        // Read tpkt len
        //
        uint32_t tpktLen = 0;
        boost::system::error_code ec;
        boost::asio::read( m_socket,
                           boost::asio::buffer( &tpktLen, sizeof(tpktLen) ),
                           asio::transfer_exactly( sizeof(tpktLen) ),
                           ec );

        //LOG( "tpktLen: " << tpktLen << std::endl );
        if ( ec || tpktLen == 0 )
        {
            m_lastErrorCode = ec;
            return false;
        }
        
        //
        // Read rest tpkt
        //
        packet.prepareToRead( tpktLen );
        boost::asio::read( m_socket,
                           boost::asio::buffer( packet.ptr()+4, tpktLen-4 ),
                           asio::transfer_exactly( tpktLen ),
                           ec );
        
        if ( ec != boost::system::errc::success || tpktLen == 0 )
        {
            m_lastErrorCode = ec;
            return false;
        }
        
        return true;
    }

//    void startReadLoop( std::function<void( Tpkt&)> ) override
//    {

//    }
};

std::unique_ptr<ITcpClient> createTcpClient()
{
    return std::unique_ptr<ITcpClient>( new TcpClient() );
}

}}
