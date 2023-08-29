// Copyright 2023 The Beam Team
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

#include <string>
#include <vector>
#include "wallet/core/wallet_db.h"

namespace beam::wallet
{
#define V7_4_API_METHODS(macro) \
        macro(SendSbbsMessage,          "send_message",       API_WRITE_ACCESS, API_SYNC, APPS_ALLOWED)    \
        macro(ReadSbbsMessages,         "read_messages",      API_READ_ACCESS, API_SYNC, APPS_ALLOWED)

    struct SendSbbsMessage
    {
        WalletID receiver = Zero;
        WalletID sender = Zero;
        ByteBuffer message;
        struct Response
        {
            WalletID sender = Zero;
            WalletID receiver = Zero;
            size_t bytes = 0;
        };
    };

    struct ReadSbbsMessages
    {
        bool all = false;
        struct Response
        {
            std::vector<InstantMessage> messages;
        };
    };
}
