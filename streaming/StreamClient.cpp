#include <vector>
#include <map>

#include "StreamClient.h"
#include "TcpClient.h"


namespace catapult {
namespace streaming {

// TcpClient
class StreamClient : public IStreamClient
{
    std::unique_ptr<net::ITcpClient> m_tcpClient;

public:
    StreamClient()
    {
        m_tcpClient = net::createTcpClient();
    }
    ~StreamClient()
    {
        LOG( "~TcpClient" << std::endl );
    }

    bool hasError() override
    {
        return m_tcpClient->hasError();
    }

    std::string errorMessage() override
    {
        return m_tcpClient->errorMessage();
    }

    bool connect( const std::string& addr, const std::string& port ) override
    {
        return m_tcpClient->connect( addr, port );
    }

    bool connect( const std::string& addr, int port ) override
    {
        return m_tcpClient->connect( addr, std::to_string(port) );
    }

    void close() override
    {
        m_tcpClient->close();
    }

    bool write( net::Tpkt& packet ) override
    {
        return m_tcpClient->write(packet);
    }

    bool read( net::TpktRcv& packet ) override
    {
        return m_tcpClient->read(packet);
    }

//    void startReadLoop( std::function<void( Tpkt&)> ) override
//    {

//    }
};

std::unique_ptr<IStreamClient> createStreamingClient()
{
    return std::unique_ptr<IStreamClient>( new StreamClient() );
}


}}
