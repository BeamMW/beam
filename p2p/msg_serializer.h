// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once
#include "protocol_base.h"
#include "utility/io/fragment_writer.h"
#include "utility/serialize.h"

namespace beam {

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
    /// If externalTailSize > 0 then serialized msg must be followed by raw buffer of thet size
    void finalize(SerializedMsg& fragments, size_t externalTailSize=0);

    /// Resets state
    void clear();

private:
    /// Fragments creator/data writer
    io::FragmentWriter _writer;

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
    /// If externalTailSize > 0 then serialized msg must be followed by raw buffer of thet size
    void finalize(SerializedMsg& fragments, size_t externalTailSize=0) {
        _os.finalize(fragments, externalTailSize);
    }

private:
    MsgSerializeOstream _os;
    yas::binary_oarchive<MsgSerializeOstream, SERIALIZE_OPTIONS> _oa;
};

} //namespace
