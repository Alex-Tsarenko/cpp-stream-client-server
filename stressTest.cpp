#include <unistd.h>

#include <iostream>
#include <thread>
#include "AsyncTcpServer.h"
#include "StreamClient.h"
#include "StreamManager.h"

inline std::mutex sLogMutex;
#define _LOG(expr) { \
        const std::lock_guard<std::mutex> autolock( sLogMutex ); \
        std::cerr << expr << std::endl << std::flush; \
    }


using namespace catapult::net;
using namespace catapult::streaming;

#define PORT        7654
#define PORT_STR    "7654"

#define STREAM_ID    "STREAM_ID_1"

void runStreamer( std::string streamerId )
{
    try
    {
        // 1) connect to server
        auto tcpClient = createStreamingClient();
        if ( !tcpClient->connect( "localhost", PORT ) )
            throw std::runtime_error( tcpClient->errorMessage() );

        // send START_STREAMING
        std::string streamId( STREAM_ID );
        StreamingTpkt pkt( 0, cmd::START_STREAMING, streamId );
        if ( !tcpClient->write(pkt) )
            throw std::runtime_error( tcpClient->errorMessage() );

        //usleep(1000*1000);
        usleep(10);

        // 2) get response
        StreamingTpktRcv response;
        if ( !tcpClient->read( (TpktRcv&)response ) )
            throw std::runtime_error( tcpClient->errorMessage() );

        // parse response
        uint32_t version;
        response.read( version );
        uint32_t responseId;
        response.read( responseId );

        // 3) check response
        if ( responseId != cmd::OK_STREAMING_RESPONSE )
        {
            if ( responseId == cmd::ERROR_STREAMING_RESPONSE )
            {
                std::string errorText;
                response.read( errorText );
                LOG( "# " << streamerId << ": ERROR_STREAMING_RESPONSE - " << errorText << std::endl );
            }
            else
            {
                LOG( "# " << streamerId << ": responseId != cmd::OK_STREAMING_RESPONSE - " << cmd::name(responseId) << std::endl );
            }
            return;
        }

        LOG( "# " << streamerId << " streaming started" << std::endl );
        for( int i=0; i<2 ; i++ )
        {
            usleep(100000);

            // 4) prepare adio/video data
            uint32_t dataLen = 20000+i;
            std::unique_ptr<uint8_t[]> buffer( new uint8_t[dataLen] );
            memset(buffer.get()+1, 0xee, dataLen-2 );
            buffer[0] = 0xaa;
            buffer[dataLen-1] = 0xaa;
            StreamingTpkt pkt( dataLen+4, cmd::STREAMING_DATA );
            pkt.writeUint32( i );
            pkt.writeBytes( buffer.get(), dataLen );

            // 5) send adio/video data
            if ( !tcpClient->write(pkt) )
                throw std::runtime_error( tcpClient->errorMessage() );
            LOG( "# " << streamerId << ": data sent; len=" << dataLen << std::endl );

            // 6) get response
            if ( !tcpClient->read( (TpktRcv&)response ) )
                throw std::runtime_error( tcpClient->errorMessage() );

            // parse response
            uint32_t version;
            response.read( version );
            uint32_t responseId;
            response.read( responseId );

            // 7) check response
            if ( responseId != cmd::OK_STREAMING_RESPONSE )
            {
                LOG( "# " << streamerId << ": streaming error; responseId != cmd::OK_STREAMING_RESPONSE - " << cmd::name(responseId) << std::endl );
                return;
            }
        }

        // 8) send END_STREAMING command
        //std::string streamId( STREAM_ID );
        StreamingTpkt pkt2( 0, cmd::END_STREAMING, streamId );
        if ( !tcpClient->write(pkt2) )
            throw std::runtime_error( tcpClient->errorMessage() );
    }
    catch ( std::runtime_error error )
    {
        LOG( "# " << streamerId << ": socket error: " << error.what() << std::endl );
    }
}

void runViewer( std::string viewerId )
{
    try {
        // 1) connect
        auto tcpClient = createStreamingClient();
        if ( !tcpClient->connect( "localhost", PORT ) )
            throw std::runtime_error( tcpClient->errorMessage() );

        // send START_LIFE_STREAM_VIEWING
        std::string streamId( STREAM_ID );
        StreamingTpkt pkt( 0, cmd::START_LIFE_STREAM_VIEWING, streamId );
        if ( !tcpClient->write(pkt) )
            throw std::runtime_error( tcpClient->errorMessage() );

        // 2) get response
        StreamingTpktRcv response;
        if ( !tcpClient->read( (TpktRcv&)response ) )
            throw std::runtime_error( tcpClient->errorMessage() );

        // parse response
        uint32_t version;
        response.read( version );
        uint32_t responseId;
        response.read( responseId );

        // 3) check response
        if ( responseId == cmd::OK_STREAMING_RESPONSE )
        {
            LOG( "# " << viewerId << ": responseId = " << cmd::name(responseId) << std::endl );
        }
        else if ( responseId == cmd::IS_NOT_STARTED_RESPONSE )
        {
            LOG( "# " << viewerId << ": responseId = " << cmd::name(responseId) << std::endl );
        }
        else
        {
            LOG( "# " << viewerId << ": responseId = " << cmd::name(responseId) << std::endl );
            return;
        }

        LOG( "# " << viewerId << ": viewinging started" << std::endl );
        uint32_t prevI = uint32_t(-1);
        for(;;)
        {
            // 4) get audio/video data
            if ( !tcpClient->read( (TpktRcv&)response ) )
                throw std::runtime_error( tcpClient->errorMessage() );
            
            //response.print("v:");

            // parse response
            uint32_t version;
            response.read( version );
            uint32_t responseId;
            response.read( responseId );

            // 5) check response id
            if ( responseId != cmd::STREAMING_DATA )
            {
                LOG( "# " << viewerId << ": viewing error; responseId != cmd::STREAMING_DATA - " << cmd::name(responseId) << std::endl );
                return;
            }

            // get i
            uint32_t i;
            response.read(i);
            if ( prevI == uint32_t(-1) )
            {
                _LOG( viewerId << " first i="<< i << std::endl );
            }
            else if ( i > prevI+1 )
            {
                _LOG( "### " << viewerId << " lost "<< i-prevI-1 << std::endl );
            }
            prevI = i;

            // get data len
            uint32_t dataLen;
            response.read(dataLen);
            LOG( "# " << viewerId << ": data received; dataLen=" << dataLen << std::endl );

            // read data
            std::unique_ptr<uint8_t[]> data( new uint8_t[dataLen] );
            response.readBytes( data.get(), dataLen );

            // check data integrity
            assert( data.get()[0]==0xaa );
            assert( data.get()[dataLen-1]==0xaa );
            for( int i=1; i<dataLen-1; i++ )
                assert( data.get()[i]==0xee );
        }
    }
    catch ( std::runtime_error error )
    {
        LOG( "# " << viewerId << ": socket error" << error.what()  << std::endl );
    }
}

int main(int, const char * [])
{
    std::thread serverThread( []
    {
        std::string errorText;
        gStreamManager().startStreamManager( PORT, 4, errorText );
    });

    std::thread streamerThread( [] { runStreamer("Streamer1"); } );

    std::vector<std::thread> viewers;
    for( int i=0; i<100; i++ )
    {
        viewers.emplace_back( [i] {
            std::string name = std::string("Viewer_") + std::to_string(i);
            runViewer( name );
        });
    }

    streamerThread.join();
    for( auto& viewer : viewers )
    {
        viewer.join();
    }

    gStreamManager().stopStreamManager();
    serverThread.join();

    return 0;
}

