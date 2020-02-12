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

#include "utility/common.h"
#include "utility/serialize_fwd.h"

namespace beam
{
    /**
     *  All content providers in broadcast network have own content type
     */
    enum class BroadcastContentType : uint32_t
    {
        SwapOffers,
        SoftwareUpdates,
        ExchangeRates
    };

    /**
     *  Inteface for different content providers
     */
    struct IBroadcastListener
    {
        virtual bool onMessage(uint64_t, ByteBuffer&&) = 0;
    };

    /**
     *  Data object broadcasted over wallet network and signed with publisher private key
     */
    struct BroadcastMsg
    {
        ByteBuffer m_content;
        ByteBuffer m_signature;

        SERIALIZE(m_content, m_signature);

        bool operator==(const BroadcastMsg& other) const
        {
            return m_content == other.m_content
                && m_signature == other.m_signature;
        };
        
        bool operator!=(const BroadcastMsg& other) const
        {
            return !(*this == other);
        };
    };

    /**
     *  Interface to access broadcasting network
     */
    struct IBroadcastMsgsGateway
    {
        virtual void registerListener(BroadcastContentType, IBroadcastListener*) = 0;
        virtual void unregisterListener(BroadcastContentType) = 0;
        virtual void sendRawMessage(BroadcastContentType type, const ByteBuffer&) = 0;
        virtual void sendMessage(BroadcastContentType type, const BroadcastMsg&) = 0;
    };
}
