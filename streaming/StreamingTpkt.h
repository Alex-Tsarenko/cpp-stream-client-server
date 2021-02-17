#pragma once
#include <vector>
#include <map>

#include "Streaming.h"
#include "Tpkt.h"

namespace catapult {
namespace streaming {

    enum { CURRENT_PROTOCOL_VERSION = 1 , PROTOCOL_VERSION = CURRENT_PROTOCOL_VERSION };

    namespace cmd
    {
        enum Id
        {
            OK_STREAMING_RESPONSE       = 100,
            ERROR_STREAMING_RESPONSE    = 101,
            IS_NOT_STARTED_RESPONSE     = 102,

            START_STREAMING             = 200,
            END_STREAMING               = 201,
            RESTORE_STREAMING           = 202,
            STREAMING_DATA              = 203,
            
            START_LIFE_STREAM_VIEWING   = 300,

            START_FILE_STREAM_VIEWING   = 400,
        };

        inline std::map<int,std::string> cmdMap =
        {
            { OK_STREAMING_RESPONSE,        "OK_STREAMING_RESPONSE" },
            { ERROR_STREAMING_RESPONSE,     "ERROR_STREAMING_RESPONSE" },
            { IS_NOT_STARTED_RESPONSE,      "IS_NOT_STARTED_RESPONSE" },

            { START_STREAMING,              "START_STREAMING" },
            { END_STREAMING,                "END_STREAMING" },
            { RESTORE_STREAMING,            "RESTORE_STREAMING" },
            { STREAMING_DATA,                "STREAMING_DATA" },

            { START_LIFE_STREAM_VIEWING,    "START_LIFE_STREAM_VIEWING" },

            { START_FILE_STREAM_VIEWING,    "START_FILE_STREAM_VIEWING" },
        };

        inline std::string name( int id )
        {
            auto it = cmdMap.find( id );
            if ( it != cmdMap.end() )
                return it->second;
            return std::string("unknown id");
        }
    }



    //
    // StreamingTpkt
    //
    class StreamingTpkt: public catapult::net::Tpkt
    {
    public:

        StreamingTpkt() {}
        
        StreamingTpkt( uint32_t restDataLen, cmd::Id command )
            : Tpkt( restDataLen, PROTOCOL_VERSION, command )
        {
        }

        StreamingTpkt( uint32_t restDataLen, cmd::Id command, const std::string& str )
            : Tpkt( uint32_t(str.size()+4+restDataLen), PROTOCOL_VERSION, command )
        {
            writeBytes( (const uint8_t*) str.c_str(), (uint32_t)str.size() );
        }

        StreamingTpkt( uint32_t restDataLen, cmd::Id command, uint8_t* ptr, uint32_t binDataLen )
            : Tpkt( binDataLen+4+restDataLen, PROTOCOL_VERSION, command )
        {
            writeBytes( ptr, binDataLen );
        }

        void init( uint32_t restDataLen, cmd::Id command )
        {
            m_buffer.erase( m_buffer.begin(), m_buffer.end() );
            m_buffer.reserve( restDataLen+12 );
            writeUint32( restDataLen+12 );
            writeUint32( PROTOCOL_VERSION );
            writeUint32( command );
        }

        void init( uint32_t restDataLen, cmd::Id command, const std::string& text )
        {
            m_buffer.erase( m_buffer.begin(), m_buffer.end() );
            m_buffer.reserve( uint32_t(text.size()+4+restDataLen+12) );
            writeUint32( uint32_t(text.size()+4+restDataLen+12) );
            writeUint32( PROTOCOL_VERSION );
            writeUint32( command );
            writeUint32( (uint32_t)text.size() );
            writeBytes( (uint8_t*)text.c_str(), (uint32_t)text.size() );
        }
        
        // !!! binData MUST HAVE length field at the beginning of data
        void initWithStreamingData( uint32_t restDataLen, cmd::Id command, const uint8_t* binData, uint32_t binDataLen )
        {
            m_buffer.erase( m_buffer.begin(), m_buffer.end() );
            m_buffer.reserve( restDataLen+4+binDataLen+12 );
            writeUint32( restDataLen+4+binDataLen+12 );
            writeUint32( PROTOCOL_VERSION );
            writeUint32( command );
            append( binData, binDataLen );
        }

        void write( const PublicKey& key )
        {
            m_buffer.insert( m_buffer.end(), key.begin(), key.end() );
        }

        void write( const BlockchainHash& key )
        {
            m_buffer.insert( m_buffer.end(), key.begin(), key.end() );
        }

        const std::vector<uint8_t>& constBuffer() const { return m_buffer; }
    };

    //
    // StreamingTpktRcv - for receiving data
    //
    class StreamingTpktRcv : public catapult::net::TpktRcv
    {
    public:
        StreamingTpktRcv() {}

//        voiprepareToReadte( uint32_t packetLenght )
//        {
//            m_buffer.reserve( packetLenght );
//            m_readPosition = &m_buffer[4];
//            m_endPosition  = &m_buffer[packetLenght];
//        }

        void read( uint32_t& num )
        {
            if ( !TpktRcv::read(num) )
                throw std::runtime_error("invalid packet size");
        }

        void read( std::string& str )
        {
            uint32_t lenght;
            read( lenght );

            if ( m_endPosition - m_readPosition < lenght )
                throw std::runtime_error("invalid packet size");

            str.reserve( lenght );
            str.assign( m_readPosition, m_readPosition+lenght );
            m_readPosition += lenght;
        }

        void read( StreamId& id )
        {
            read( id.m_id );
        }

        void readBytes( uint8_t* ptr, uint32_t lenght )
        {
            if ( m_endPosition - m_readPosition < lenght )
                throw std::runtime_error("invalid packet size");

            memcpy( ptr, m_readPosition, lenght );
            m_readPosition += lenght;
        }
    };

}} // namespace catapult { namespace streaming
