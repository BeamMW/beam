#pragma once
#include "protocol_base.h"
#include "fragment_writer.h"
#include "utility/serialize.h"

namespace beam {

/// May have fragments...
using SerializedMsg = std::vector<io::SharedBuffer>;

/// Accumulates messages being serialized into shared fragments of memory,
/// takes care of proper message header
class MsgSerializeOstream {
public:
    /// Ctor setups fragment collector
    explicit MsgSerializeOstream(size_t fragmentSize, MsgHeader defaultHeader);

    /// Called by msg serializer on new message
    void new_message(MsgType type);

    /// Called by yas serializeron new data
    size_t write(const void *ptr, size_t size);

    /// Called by msg serializer on finalizing msg
    void finalize(SerializedMsg& fragments);

    /// Resets state
    void clear();

private:
    /// Fragments creator/data writer
    FragmentWriter _writer;

    /// Fragments of current message
    SerializedMsg _fragments;

    /// Total size of message
    size_t _currentMsgSize=0;

    /// Pointer to msg header which is filled on finalize() when size is known
    void* _currentHeaderPtr=0;

    /// Current header
    MsgHeader _currentHeader;
};

/// Serializes protocol messages (of arbitrary sizes) into shared fragments using MsgSerializeOstream
class MsgSerializer {
public:
    explicit MsgSerializer(size_t fragmentSize, MsgHeader defaultHeader) :
        _os(fragmentSize, defaultHeader),
        _oa(_os)
    {}

    /// Begins a new message
    void new_message(MsgType type) {
        _os.new_message(type);
    }

    /// Serializes whatever in message
    template <typename T> MsgSerializer& operator&(const T& object) {
        _oa & object;
        return *this;
    }

    /// Finalizes current message serialization. Returns serialized data in fragments
    void finalize(SerializedMsg& fragments) {
        _os.finalize(fragments);
    }

private:
    MsgSerializeOstream _os;
    yas::binary_oarchive<MsgSerializeOstream, SERIALIZE_OPTIONS> _oa;
};

} //namespace
