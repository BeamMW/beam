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

#include "msg_serializer.h"
#include <assert.h>

namespace beam {

MsgSerializeOstream::MsgSerializeOstream(size_t fragmentSize, MsgHeader defaultHeader) :
    _writer(
        fragmentSize,
        MsgHeader::SIZE,
        [this](io::SharedBuffer&& f) {
            _currentMsgSize += f.size;
            _fragments.push_back(std::move(f));
        }
    ),
    _currentHeader(defaultHeader)
{}

void MsgSerializeOstream::new_message(MsgType type) {
    assert(_currentMsgSize == 0 && _currentHeaderPtr == 0);
    _currentHeader.type = type;
    _currentHeaderPtr = _writer.write(&_currentHeader, MsgHeader::SIZE);
}

size_t MsgSerializeOstream::write(const void *ptr, size_t size) {
    assert(_currentHeaderPtr != 0);
    _writer.write(ptr, size);
    return size;
}

void MsgSerializeOstream::finalize(SerializedMsg& fragments, size_t externalTailSize) {
    assert(_currentHeaderPtr != 0);
    _writer.finalize();
    assert(_currentMsgSize >= MsgHeader::SIZE);
    assert(externalTailSize <= 0xFFFFFFFF - uint32_t(_currentMsgSize - MsgHeader::SIZE));
    _currentHeader.size = uint32_t(_currentMsgSize - MsgHeader::SIZE + externalTailSize);
    _currentHeader.write(_currentHeaderPtr);
    fragments.swap(_fragments);
    clear();
}

void MsgSerializeOstream::clear() {
    _fragments.clear();
    _currentMsgSize = 0;
    _currentHeaderPtr = 0;
}

} //namespace
