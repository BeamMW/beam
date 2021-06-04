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
#include "wallet/core/common.h"

namespace beam::wallet
{
    typedef std::pair<Asset::ID, Asset::ID> DexMarket;

    enum class DexMarketSide: uint32_t
    {
        Sell,
        Buy
    };

    class DexOrder
    {
    public:
        SERIALIZE(version, orderID, sbbsID, sbbsKeyIDX, market.first, market.second, side, origSize, origPrice, remainingSize, expireTime);
        static uint32_t getCurrentVersion();

        DexOrder() = default;
        DexOrder(const ByteBuffer& buffer, const ByteBuffer& signature, beam::Key::IKdf::Ptr);
        DexOrder(DexOrderID orderId,
                 WalletID   sbbsId,
                 uint64_t   sbbsKeyIdx,
                 DexMarket  market,
                 DexMarketSide side,
                 Amount     size,
                 Amount     price,
                 Timestamp  expiration);

        bool operator==(const DexOrder& other) const
        {
            return orderID == other.orderID;
        }

        [[nodiscard]] uint32_t getVersion() const;
        [[nodiscard]] bool IsMine() const;
        [[nodiscard]] bool IsExpired() const;
        [[nodiscard]] bool IsCompleted() const;
        [[nodiscard]] bool CanAccept() const;
        [[nodiscard]] const DexOrderID& getID() const;
        [[nodiscard]] const WalletID& getSBBSID() const;
        [[nodiscard]] Timestamp getExpiration() const;
        [[nodiscard]] DexMarketSide getSide() const;
        [[nodiscard]] Amount getPrice() const;
        [[nodiscard]] Amount getSize() const;

        //
        // These are to easily create transactions
        //
        [[nodiscard]] Asset::ID getISendCoin() const;
        [[nodiscard]] Asset::ID getIReceiveCoin() const;
        [[nodiscard]] Amount getISendAmount() const;
        [[nodiscard]] Amount getIReceiveAmount() const;

        void LogInfo() const;

        [[nodiscard]] ECC::Scalar::Native derivePrivateKey(beam::Key::IKdf::Ptr) const;
        [[nodiscard]] PeerID derivePublicKey(beam::Key::IKdf::Ptr) const;

    private:
        uint32_t      version = 0;
        DexOrderID    orderID;      // UUID
        WalletID      sbbsID;       // here wallet listens for order processing
        uint64_t      sbbsKeyIDX = 0; // index used to generate SBBS key, to identify OUR orders
        DexMarket     market;
        DexMarketSide side;

        Amount  origSize = 0;
        Amount  origPrice = 0;
        Amount  remainingSize = 0;

        bool       isMine     = false;
        Timestamp  expireTime = 0;
    };
}
