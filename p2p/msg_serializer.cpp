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

void MsgSerializeOstream::finalize(std::vector<io::SharedBuffer>& fragments) {
    assert(_currentHeaderPtr != 0);
    _writer.finalize();
    assert(_currentMsgSize >= MsgHeader::SIZE);
    _currentHeader.size = uint32_t(_currentMsgSize - MsgHeader::SIZE);
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
