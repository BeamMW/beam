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

#include "offers_protocol_handler.h"

namespace beam::wallet
{
    boost::optional<ByteBuffer> OfferBoardProtocolHandler::createMessage(const SwapOfferToken& content, const BbsChannel& channel, const WalletID& wid)
    {
        constexpr uint8_t MessageType = 0;
        auto waddr = m_walletDB->getAddress(wid);

        if (waddr && waddr->isOwn())
        {
            // Get private key
            PrivateKey sk;
            PublicKey pk;
            m_sbbsKdf->DeriveKey(sk, ECC::Key::ID(waddr->m_OwnID, Key::Type::Bbs));
            proto::Sk2Pk(pk, sk);

            // Sign data with private key
            SwapOfferConfirmation confirmationBuilder;
            auto& contentRaw = confirmationBuilder.m_offerData;
            contentRaw = toByteBuffer(content);
            confirmationBuilder.Sign(sk);
            auto signatureRaw = toByteBuffer(confirmationBuilder.m_Signature);

            // Create message header according to protocol
            size_t msgBodySize = contentRaw.size() + signatureRaw.size();
            assert(msgBodySize <= UINT32_MAX);
            MsgHeader header(0, 0, m_protocolVersion, MessageType, static_cast<uint32_t>(msgBodySize));

            // Combine all to final message
            ByteBuffer finalMessage(header.SIZE);
            header.write(finalMessage.data());  // copy header to finalMessage
            finalMessage.reserve(header.SIZE + header.size);
            std::copy(  std::begin(contentRaw),
                        std::end(contentRaw),
                        std::back_inserter(finalMessage));
            std::copy(  std::begin(signatureRaw),
                        std::end(signatureRaw),
                        std::back_inserter(finalMessage));
            
            return finalMessage;
        }
        return boost::none;
    };

} // namespace beam::wallet