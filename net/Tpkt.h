#pragma once
#include <vector>

//#ifdef DEBUG
//#include <iomanip>
//#endif

#include "Streaming.h"

namespace catapult {
namespace net {

    // TpktLen
    struct TpktLen
    {
        uint8_t bytes[4];

        TpktLen() {}

        TpktLen( uint32_t len )
        {
            bytes[0] = len         & 0xFF;
            bytes[1] = (len >>  8) & 0xFF;
            bytes[2] = (len >> 16) & 0xFF;
            bytes[3] = (len >> 14) & 0xFF;
        }
        
        uint32_t uint32() const
        {
            return (bytes[0]) | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24);
        }
    };


    //
    // Tpkt - transport packet: { packetLenght, data ... }
    //
    class Tpkt
    {
    protected:
        std::vector<uint8_t> m_buffer;

        Tpkt() {}

        Tpkt( uint32_t restDataLen, uint32_t version, uint32_t command )
        {
            m_buffer.reserve( restDataLen+12 );
            writeUint32( restDataLen+12 );
            writeUint32( version );
            writeUint32( command );
        }
    public:

        // updatePacketLenght
        void updatePacketLenght()
        {
            uint32_t len = m_buffer.size();
            m_buffer[0] = len         & 0xFF;
            m_buffer[1] = (len >>  8) & 0xFF;
            m_buffer[2] = (len >> 16) & 0xFF;
            m_buffer[3] = (len >> 24) & 0xFF;
        }

        // writeUint32
        void writeUint32( uint32_t val )
        {
            m_buffer.push_back( (val      ) & 0xFF );
            m_buffer.push_back( (val >>  8) & 0xFF );
            m_buffer.push_back( (val >> 16) & 0xFF );
            m_buffer.push_back( (val >> 24) & 0xFF );
        }

        // writeBytes
        void writeBytes( const uint8_t* bytes, uint32_t len )
        {
            writeUint32( len );
            if ( len > 0 )
            {
                m_buffer.insert( m_buffer.end(), bytes, bytes+len );
            }
        }
        
        void append( const uint8_t* bytes, uint32_t len )
        {
            if ( len > 0 )
            {
                m_buffer.insert( m_buffer.end(), bytes, bytes+len );
            }
        }
        
//#ifdef DEBUG
//        void print( std::string title )
//        {
//            std::cout << title;
//            for( auto& it : m_buffer )
//            {
//                std::cout << std::hex << uint(it) << " ";
//            }
//            std::cout << std::endl;
//        }
//#endif

        size_t          lenght()      const { return m_buffer.size(); }
        const uint8_t*  ptr()         const { return &m_buffer[0]; }
    };

    //
    // TpktRcv - for receiving data
    //
    class TpktRcv : protected net::Tpkt
    {
    protected:
        uint8_t* m_readPosition;
        uint8_t* m_endPosition;

    public:
        TpktRcv() {}

        TpktRcv( uint32_t packetLenght ) : net::Tpkt( 0, -1, -1)
        {
            prepareToRead( packetLenght );
        }
        
        uint8_t*       ptr()               { return &m_buffer[0]; }
        const uint32_t restDataLen() const { return uint32_t(m_endPosition - m_readPosition); }
        const uint8_t* restDataPtr() const { return m_readPosition; }

        void prepareToRead( uint32_t packetLenght )
        {
            m_buffer.reserve( packetLenght );
            m_buffer[0] = packetLenght         & 0xFF;
            m_buffer[1] = (packetLenght >>  8) & 0xFF;
            m_buffer[2] = (packetLenght >> 16) & 0xFF;
            m_buffer[3] = (packetLenght >> 14) & 0xFF;

            m_readPosition = &m_buffer[4];
            m_endPosition  = &m_buffer[packetLenght];
        }

        bool read( uint32_t& val )
        {
            if ( m_endPosition - m_readPosition < 4 )
                return false;

            val = *m_readPosition++;
            val |= *m_readPosition++ << 8;
            val |= *m_readPosition++ << 16;
            val |= *m_readPosition++ << 24;

            return true;
        }

//#ifdef DEBUG
//        void print( std::string title )
//        {
//            std::cout << title;
//            for( auto it = m_readPosition; it!=m_endPosition; it++ )
//            {
//                std::cout << std::hex << uint(*it) << " ";
//            }
//            std::cout << std::endl;
//        }
//#endif
    };


}} // namespace catapult { namespace streaming
