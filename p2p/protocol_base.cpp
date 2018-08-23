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

#include "protocol_base.h"
#include "utility/logger.h"

namespace beam {

bool ProtocolBase::on_new_message(uint64_t fromStream, MsgType type, const void* data, size_t size) {
    OnRawMessage callback = _dispatchTable[type].callback;
    if (!callback) {
        LOG_WARNING() << "Unexpected msg type " << int(type);
        _errorHandler.on_protocol_error(fromStream, msg_type_error);
        return false;
    }
    LOG_VERBOSE() << __FUNCTION__ << TRACE(int(type));
    bool ret = callback(_dispatchTable[type].msgHandler, _errorHandler, *_deserializer, fromStream, data, size);
    if (!ret) {
        LOG_ERROR() << __FUNCTION__ << TRACE(int(type)) << TRACE(ret);
    }
    return ret;
}

} //namespace
