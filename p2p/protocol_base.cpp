#include "protocol_base.h"
#include "utility/logger.h"

namespace beam {

bool ProtocolBase::on_new_message(uint64_t fromStream, MsgType type, const void* data, size_t size) {
    OnRawMessage callback = _dispatchTable[type].callback;
    if (!callback) {
        _errorHandler.on_protocol_error(fromStream, msg_type_error);
        return false;
    }
    LOG_VERBOSE() << __FUNCTION__ << TRACE(int(type));
    bool ret = callback(_dispatchTable[type].msgHandler, _errorHandler, *_deserializer, fromStream, data, size);
    LOG_VERBOSE() << __FUNCTION__ << TRACE(int(type)) << TRACE(ret);
    return ret;
}

} //namespace
