// Copyright 2020 The Beam Team
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

#ifdef BEAM_IPFS_SUPPORT
#include <asio-ipfs/include/ipfs_config.h>
#else
namespace asio_ipfs {
    // This is just a stub to compile constructors when there is no IPFS library
    struct config {
        enum class Mode {
            Desktop,
            Server,
        };
        explicit config (enum Mode) {
        }
    };
}
#endif
