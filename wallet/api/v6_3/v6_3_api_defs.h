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
#pragma once

#include <vector>
#include <memory>
#include <string>

namespace beam::wallet
{
    #define V6_3_API_METHODS(macro) \
        macro(IPFSAdd,   "ipfs_add",   API_WRITE_ACCESS, API_ASYNC, APPS_ALLOWED) \
        macro(IPFSGet,   "ipfs_get",   API_WRITE_ACCESS, API_ASYNC, APPS_ALLOWED) \
        macro(IPFSPin,   "ipfs_pin",   API_WRITE_ACCESS, API_ASYNC, APPS_ALLOWED) \
        macro(IPFSUnpin, "ipfs_unpin", API_WRITE_ACCESS, API_ASYNC, APPS_ALLOWED) \
        macro(IPFSGc,    "ipfs_gc",    API_WRITE_ACCESS, API_ASYNC, APPS_ALLOWED)

    struct IPFSAdd
    {
        std::vector<uint8_t> data;
        uint32_t timeout = 0;

        struct Response
        {
            std::string hash;
        };
    };

    struct IPFSGet
    {
        std::string hash;
        uint32_t timeout = 0;

        struct Response
        {
            std::string hash;
            std::vector<uint8_t> data;
        };
    };

    struct IPFSPin
    {
        std::string hash;
        uint32_t timeout = 0;

        struct Response
        {
            std::string hash;
        };
    };

    struct IPFSUnpin
    {
        std::string hash;
        uint32_t timeout = 0;

        struct Response
        {
            std::string hash;
        };
    };

    struct IPFSGc
    {
        uint32_t timeout = 0;
        struct Response
        {
        };
    };
}
