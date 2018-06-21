#pragma once
#include "protocol.h"

namespace beam {

class Protocol;

class CommonMessages {
public:
    explicit CommonMessages(Protocol& protocol);

    const io::SharedBuffer& get(MsgType type) const;

    template <typename MsgObject> void update(MsgType type, const MsgObject& obj) {
        if (type < _table.size()) {
            _protocol.serialize(_fragments, type, obj);
            _table[type] = io::normalize(_fragments);
            _fragments.clear();
        }
    }

private:
    Protocol& _protocol;
    std::vector<io::SharedBuffer> _table;
    SerializedMsg _fragments;
};

} //namespace
