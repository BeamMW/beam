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

#include "buffer.h"

namespace beam { namespace io {

SharedBuffer normalize(const SerializedMsg& msg, bool makeUnique) {
    size_t n = msg.size();
    if (n==0) return SharedBuffer();

    if (n==1) {
        if (makeUnique) {
            // copies
            return SharedBuffer(msg[0].data, msg[0].size);
        } else {
            return msg[0];
        }
    }

    size_t size = 0;
    for (const auto& fr : msg) {
        size += fr.size;
    }
    uint8_t* data = (uint8_t*)malloc(size);
    uint8_t* ptr = data;

    // TODO handle data==0

    for (const auto& fr : msg) {
        memcpy(ptr, fr.data, fr.size);
        ptr += fr.size;
    }
    return SharedBuffer(data, size, SharedMem(data, [](void* p) { free(p); }));
}

}} //namespaces
