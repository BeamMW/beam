#pragma once
#include <stdint.h>
#include <string.h>

namespace beam { namespace protocol {

/// Message type
enum class MsgType : uint8_t {
    null,               // 0
    hand,               // 1
    shake,              // 2
    ping,               // 3
    pong,               // 4
    peerAddrsRequest,   // 5
    peerAddrs,          // 6
    headersRequest,     // 7
    header,             // 8
    headers,            // 9
    blockRequest,       // 10
    block,              // 11
    compactBlockRequest,// 12
    compactBlock,       // 13
    transaction,        // 14
    txHashSetRequest,   // 15
    txHashSet,          // 16
    // ~etc
    invalid_type        // 17
};
    
/// Message header
struct MsgHeader {
    /// Serialized header size
    static constexpr size_t SIZE = 8;
    
    /// Protocol ID, contains magic bits & version (==1)
    static constexpr uint8_t protocol[3] = { 0xBE, 0xA0, 0x01 };
    
    /// Minimal serialized message size by type
    static constexpr uint32_t minSizes[] = {
        // TODO real sizes
        0, // null
        8, // hand
        8, // shake
        8, // ping
        8, // pong
        8, // peerAddrsRequest
        8, // peerAddrs
        8, // headersRequest
        8, // header
        8, // headers
        8, // blockRequest
        8, // block
        8, // compactBlockRequest
        8, // compactBlock
        8, // transaction
        8, // txHashSetRequest
        8  // txHashSet
    };

    /// Maximal serialized message size by type
    static constexpr uint32_t maxSizes[] = {
        // TODO real sizes
        0, // null
        1 << 24, // hand
        1 << 24, // shake
        1 << 24, // ping
        1 << 24, // pong
        1 << 24, // peerAddrsRequest
        1 << 24, // peerAddrs
        1 << 24, // headersRequest
        1 << 24, // header
        1 << 24, // headers
        1 << 24, // blockRequest
        1 << 24, // block
        1 << 24, // compactBlockRequest
        1 << 24, // compactBlock
        1 << 24, // transaction
        1 << 24, // txHashSetRequest
        1 << 24  // txHashSet
    };
    
    /// Message type, 1 byte
    MsgType type;
    
    /// Size of underlying serialized message
    uint32_t size;
    
    MsgHeader() : type(MsgType::null), size(0) {}
    
    MsgHeader(MsgType _type, uint32_t _size) : type(_type), size(_size) {}
    
    /// Reads from stream, verifies header
    /// NOTE: caller makes sure that src points to at least SIZE bytes
    bool read(const void* src) {
        const uint8_t* p = (const uint8_t*)src;
        if (p[0] != protocol[0] ||
            p[1] != protocol[1] ||
            p[2] != protocol[2] ||
            p[3] >= uint8_t(MsgType::invalid_type)
        ) {
            return false;
        }
        type = MsgType(p[3]);
        memcpy(&size, p+3, 4);
        return true;
    }
    
    /// Verifies min/max size of incoming message
    bool verify_size() {
        size_t i = size_t(type);
        return (i < uint8_t(MsgType::invalid_type) && size >= minSizes[i] && size <= maxSizes[i]);
    }
    
    void write(void* dst) {
        uint8_t* p = (uint8_t*)dst;
        *p++ = protocol[0];
        *p++ = protocol[1];
        *p++ = protocol[2];
        *p++ = uint8_t(type);
        memcpy(p, &size, 4);
    }
};



}} //namespaces
