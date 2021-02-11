#pragma once
#include <stdlib.h>

//
// For standalone debugging
//
#include <iostream>

inline std::mutex gLogMutex;

#define LOG(expr) { \
        const std::lock_guard<std::mutex> autolock( gLogMutex ); \
        std::cerr << expr << std::flush; \
    }

#define LOG_ERR(expr) { \
    std::cerr << __FILE__ << ":" << __LINE__ << ": "<< expr << std::flush; \
    std::cerr << expr << std::flush; \
}


namespace catapult {
namespace streaming {

    // PublicKey
    struct PublicKey
    {
        enum { SIZE = 32 };
        uint8_t key[SIZE];

        const uint8_t* begin() const { return key; }
        const uint8_t* end() const { return key + SIZE; }
    };

    // BlockchainHash
    struct BlockchainHash
    {
        enum { SIZE = 32 };
        uint8_t key[SIZE];

        const uint8_t* begin() const { return key; }
        const uint8_t* end() const { return key + SIZE; }
    };

    // StreamId
    struct StreamId
    {
        std::string m_id;

        StreamId() {}
        StreamId( const std::string& id ) : m_id(id) {}

        uint32_t lenght()       const { return (uint32_t) m_id.size(); }
        const uint8_t* begin()  const { return (uint8_t*) m_id.c_str(); }
        const uint8_t* end()    const { return (uint8_t*) m_id.c_str()+m_id.size(); }

        bool operator<( const StreamId& id ) const { return m_id < id.m_id; }
//        bool operator<( const StreamId& other ) const { return memcmp( id, other.id, sizeof(id) ) < 0; }
    };
}} // namespace catapult { namespace streaming
