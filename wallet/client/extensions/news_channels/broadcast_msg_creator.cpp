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

#include "broadcast_msg_creator.h"
#include "wallet/core/common.h"
#include "core/proto.h"

namespace beam::wallet
{
    using PrivateKey = ECC::Scalar::Native;

    bool BroadcastMsgCreator::stringToPrivateKey(const std::string& key, PrivateKey& out)
    {
        bool resultIsKeyStrValid = true;
        ByteBuffer keyBytes = from_hex(key, &resultIsKeyStrValid);
        if (!resultIsKeyStrValid)
        {
            return false;
        }

        Blob keyBlob(keyBytes.data(), static_cast<uint32_t>(keyBytes.size()));

        ECC::Scalar keyScalar;
        keyScalar.m_Value = ECC::uintBig(keyBlob);

        if (keyScalar.IsValid())
        {
            out.Import(keyScalar);
        }
        else
        {
            return false;
        }

        return true;
    }

    BroadcastMsg BroadcastMsgCreator::createSignedMessage(const ByteBuffer& content, const PrivateKey& key)
    {
        BroadcastMsg msg;
        msg.m_content = content;

        SignatureHandler signHandler;
        signHandler.m_data = content;
        signHandler.Sign(key);
        msg.m_signature = toByteBuffer(signHandler.m_Signature);

        return msg;
    }

} // namespace beam::wallet
