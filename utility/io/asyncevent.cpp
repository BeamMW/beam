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

#include "asyncevent.h"
#include <assert.h>

namespace beam { namespace io {

AsyncEvent::Ptr AsyncEvent::create(Reactor& reactor, AsyncEvent::Callback&& callback) {
    assert(callback);

    if (!callback)
        IO_EXCEPTION(EC_EINVAL);

    Ptr event(new AsyncEvent(std::move(callback)));
    ErrorCode errorCode = reactor.init_asyncevent(
        event.get(),
        [](uv_async_t* handle) {
            assert(handle);
            AsyncEvent* ae = reinterpret_cast<AsyncEvent*>(handle->data);
            if (ae) ae->_callback();
        }
    );
    IO_EXCEPTION_IF(errorCode);
    return event;
}

AsyncEvent::AsyncEvent(AsyncEvent::Callback&& callback) :
    _callback(std::move(callback)) //, _valid(true)
{}

AsyncEvent::~AsyncEvent() {
    // before async_close
    //_valid = false;
}

Result AsyncEvent::post() {
    ErrorCode errorCode = (ErrorCode)uv_async_send((uv_async_t*)_handle);
    return make_result(errorCode);
}

}} //namespaces

