// Copyright 2022 The Beam Team
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
    class AssetSwapOrder
    {
    public:
        SERIALIZE(_version, _orderID, _sbbsID, _sbbsKeyIDX, _assetIdFirst, _assetIdSecond, _assetSnameFirst, _assetSnameSecond, _assetAmountFirst, _assetAmountSecond, _expireTime, _isCompleted);
        static const uint32_t kCurrentOfferVer = 8;
        static uint32_t getCurrentVersion() { return kCurrentOfferVer;}

        AssetSwapOrder() = default;
        AssetSwapOrder(const ByteBuffer& buffer, const ByteBuffer& signature, beam::Key::IKdf::Ptr);
        AssetSwapOrder(DexOrderID    orderId,
                       WalletID      sbbsId,
                       uint64_t      sbbsKeyIdx,
                       Asset::ID     assetIdFirst,
                       Amount        assetAmountFirst,
                       std::string   assetSnameFirst,
                       Asset::ID     assetIdSecond,
                       Amount        assetAmountSecond,
                       std::string   assetSnameSecond,
                       Timestamp     expiration);

        bool operator==(const AssetSwapOrder& other) const
        {
            return _orderID == other._orderID;
        }

        [[nodiscard]] uint32_t getVersion() const;
        [[nodiscard]] bool isMine() const;
        [[nodiscard]] bool isExpired() const;
        [[nodiscard]] const DexOrderID& getID() const;
        [[nodiscard]] const WalletID& getSBBSID() const;
        [[nodiscard]] Timestamp getExpiration() const;
        [[nodiscard]] Amount getFirstAmount() const;
        [[nodiscard]] Amount getSecondAmount() const;
        [[nodiscard]] Asset::ID getFirstAssetId() const;
        [[nodiscard]] Asset::ID getSecondAssetId() const;
        [[nodiscard]] std::string getFirstAssetSname() const;
        [[nodiscard]] std::string getSecondAssetSname() const;

        [[nodiscard]] ECC::Scalar::Native derivePrivateKey(beam::Key::IKdf::Ptr) const;
        [[nodiscard]] PeerID derivePublicKey(beam::Key::IKdf::Ptr) const;

        Amount getSendAmount() const;
        Amount getReceiveAmount() const;
        Asset::ID getSendAssetId() const;
        Asset::ID getReceiveAssetId() const;
        std::string getSendAssetSName() const;
        std::string getReceiveAssetSName() const;

    private:
        uint32_t      _version = 1;
        DexOrderID    _orderID;      // UUID
        WalletID      _sbbsID;       // here wallet listens for order processing
        uint64_t      _sbbsKeyIDX = 0; // index used to generate SBBS key, to identify OUR orders

        Asset::ID     _assetIdFirst = 0;
        Asset::ID     _assetIdSecond = 0;
        std::string   _assetSnameFirst;
        std::string   _assetSnameSecond;
        Amount        _assetAmountFirst = 0;
        Amount        _assetAmountSecond = 0;

        bool          _isMine     = false;
        Timestamp     _expireTime = 0;
        bool          _isCompleted = false;
    };
}
