#include <unistd.h>

#include <iostream>
#include <thread>
#include "AsyncTcpServer.h"
#include "StreamClient.h"
#include "StreamManager.h"

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

        usleep(1000000);

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
            uint32_t dataLen = 1000+i;
            uint8_t buffer[dataLen];
            memset(buffer+1, 0xee, dataLen-2 );
            buffer[0] = 0xaa;
            buffer[dataLen-1] = 0xaa;
            StreamingTpkt pkt( dataLen, cmd::STREAMING_DATA );
            pkt.writeBytes( buffer, dataLen );

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
        for(;;)
        {
            // 4) get audio/video data
            if ( !tcpClient->read( (TpktRcv&)response ) )
                throw std::runtime_error( tcpClient->errorMessage() );

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
    std::thread t( []
    {
        std::string errorText;
        gStreamManager().startStreamManager( PORT, errorText );
    });

    std::thread s1( [] { runStreamer("Streamer1"); } );

    std::thread v1( [] { runViewer("Viewer1"); } );


    s1.join();
    v1.join();

    gStreamManager().stopStreamManager();
    t.join();

    return 0;
}

