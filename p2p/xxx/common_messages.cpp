#include "common_messages.h"

namespace beam {

CommonMessages::CommonMessages(Protocol& protocol) :
    _protocol(protocol)
{
    _table.resize(_protocol.max_message_types());
}

const io::SharedBuffer& CommonMessages::get(MsgType type) const {
    static const io::SharedBuffer dummy;
    if (type > _table.size()) return dummy;
    return _table[type];
}

} //namespace
