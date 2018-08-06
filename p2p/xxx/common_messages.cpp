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
