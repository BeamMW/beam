// Copyright 2019 The Beam Team
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

#include "newscast_protocol_builder.h"
#include "wallet/core/common.h"
#include "core/proto.h"
#include "p2p/protocol_base.h"

namespace beam::wallet
{
    using PrivateKey = ECC::Scalar::Native;

    boost::optional<PrivateKey> NewscastProtocolBuilder::stringToPrivateKey(const std::string& key)
    {
        bool resultIsKeyStrValid = true;
        ByteBuffer keyBytes = from_hex(key, &resultIsKeyStrValid);
        if (!resultIsKeyStrValid)
        {
            return boost::none;
        }

        Blob keyBlob(keyBytes.data(), static_cast<uint32_t>(keyBytes.size()));

        ECC::Scalar keyScalar;
        keyScalar.m_Value = ECC::uintBig(keyBlob);

        ECC::Scalar::Native nativeKey;
        if (keyScalar.IsValid())
        {
            nativeKey.Import(keyScalar);
        }
        else
        {
            return boost::none;
        }

        return nativeKey;
    }

    ByteBuffer NewscastProtocolBuilder::createMessage(const NewsMessage& msg, const PrivateKey& key)
    {
        ByteBuffer content = toByteBuffer(msg);
        SignatureHandler signHandler;
        signHandler.m_data = content;
        signHandler.Sign(key);
        ByteBuffer signature = toByteBuffer(signHandler.m_Signature);

        ByteBuffer fullMsg(MsgHeader::SIZE);
        size_t rawBodySize = content.size() + signature.size();
        assert(rawBodySize <= proto::Bbs::s_MaxMsgSize);

        MsgHeader header(0, 0, 1, 1, static_cast<uint32_t>(rawBodySize));
        header.write(fullMsg.data());

        std::copy(  std::begin(content),
                    std::end(content),
                    std::back_inserter(fullMsg));
        std::copy(  std::begin(signature),
                    std::end(signature),
                    std::back_inserter(fullMsg));

        return fullMsg;
    }

} // namespace beam::wallet
