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

#pragma once

#include "swap_offer_token.h"
#include "wallet/core/wallet_db.h"
#include "wallet/client/extensions/broadcast_gateway/interface.h"

namespace beam::wallet
{
    using namespace beam::proto;

    class OfferBoardProtocolHandler
    {
        using PrivateKey = ECC::Scalar::Native;
        using PublicKey = PeerID;

    public:

        // With time possible to split this class to MessageCreator with access to KDF
        // and MessageParser without access

        /**
         *  Create message with swap offer according to protocol.
         *  Message includes signature and pubKey for validation.
         */
        explicit OfferBoardProtocolHandler(ECC::Key::IKdf::Ptr sbbsKdf);
    
        /**
         * Create message signed with private key
         *
         * @param content   Swap offer data
         * @param keyOwnID  Signatory's BBS key ID, used to derive Sk to create signature
         */
        ByteBuffer createMessage(const SwapOffer& content, uint64_t keyOwnID) const; // Deprecated. TODO: dh remove after 2 fork
        BroadcastMsg createBroadcastMessage(const SwapOffer& content, uint64_t keyOwnID) const;

        /**
         *  Parse message and verify signature.
         */
        boost::optional<SwapOffer> parseMessage(const ByteBuffer& rawMessage) const;    // Deprecated. TODO: dh remove after 2 fork.
        boost::optional<SwapOffer> parseMessage(const BroadcastMsg& msg) const;

    private:
        std::shared_ptr<beam::wallet::IWalletDB> m_walletDB;
        std::shared_ptr<ECC::Key::IKdf> m_sbbsKdf;

        static constexpr uint8_t m_protocolVersion = 1;
        static constexpr uint8_t m_msgType = 0;
    };
    
} // namespace beam::wallet
