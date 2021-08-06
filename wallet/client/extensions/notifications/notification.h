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

#include "core/ecc.h"

namespace beam::wallet
{
    struct Notification
    {
        enum class Type : uint32_t
        {
            SoftwareUpdateAvailable,  // deprecated
            AddressStatusChanged,
            WalletImplUpdateAvailable,
            BeamNews,
            TransactionFailed,
            TransactionCompleted,
            // extend range check in jni.cpp on adding new type
        };

        enum class State : uint32_t
        {
            Unread,
            Read,
            Deleted
        };

        ECC::uintBig m_ID;  // unique
        Type m_type;
        State m_state;
        Timestamp m_createTime;
        ByteBuffer m_content;

        bool operator==(const Notification& other) const
        {
            return m_ID == other.m_ID
                && m_type == other.m_type
                && m_state == other.m_state
                && m_createTime == other.m_createTime
                && m_content == other.m_content;
        };
    };

    class TxParameters;
    TxParameters getTxParameters(const Notification &notification);
} // namespace beam::wallet
