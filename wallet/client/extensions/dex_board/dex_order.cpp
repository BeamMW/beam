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
#include "dex_order.h"
#include "utility/logger.h"
#include "core/common.h"

namespace beam::wallet
{
    namespace
    {
        const uint32_t kCurrentOfferVer = 6;
    }

    uint32_t DexOrder::getCurrentVersion()
    {
        return kCurrentOfferVer;
    }

    DexOrder::DexOrder(const ByteBuffer& content, const ByteBuffer& signature, beam::Key::IKdf::Ptr pkdf)
    {
        if (!fromByteBuffer(content, *this))
        {
            throw std::runtime_error("DexOrder::DexOrder failed to parse order body");
        }

        if (version != DexOrder::getCurrentVersion())
        {
            throw std::runtime_error("DexOrder::DexOrder obsolete order version");
        }

        SignatureHandler sig;
        sig.m_data = content;

        if (!fromByteBuffer(signature, sig.m_Signature))
        {
            throw std::runtime_error("DexOrder::DexOrder failed to parse signature");
        }

        if (!sig.IsValid(sbbsID.m_Pk))
        {
            throw std::runtime_error("DexOrder::DexOrder  failed to verify signature");
        }

        auto pubkey = derivePublicKey(pkdf);
        isMine = pubkey == sbbsID.m_Pk;
    }

    DexOrder::DexOrder(DexOrderID orderId,
                 WalletID   sbbsId,
                 uint64_t   sbbsKeyIdx,
                 DexMarket  market,
                 DexMarketSide side,
                 Amount     size,
                 Amount     price,
                 Timestamp  expiration)
        : version(kCurrentOfferVer)
        , orderID(orderId)
        , sbbsID(sbbsId)
        , sbbsKeyIDX(sbbsKeyIdx)
        , market(std::move(market))
        , side(side)
        , origSize(size)
        , origPrice(price)
        , remainingSize(size)
        , isMine(true)
        , expireTime(expiration)
    {
    }

    uint32_t DexOrder::getVersion() const
    {
        return version;
    }

    bool DexOrder::IsExpired() const
    {
        const auto now = beam::getTimestamp();
        return expireTime <= now;
    }

    bool DexOrder::IsCompleted() const
    {
        return remainingSize == 0;
    }

    bool DexOrder::CanAccept() const
    {
        return !isMine && !IsExpired() && !IsCompleted();
    }

    void DexOrder::LogInfo() const
    {
        /*
        if (isMine)
        {
            LOG_INFO()
                << "\tI sell " << PrintableAmount(sellAmount, true, sellCoin)
                << "\tI buy  " << PrintableAmount(getBuyAmount(), true, buyCoin);
        }
        else
        {
            LOG_INFO()
                << "\tOther sells " << PrintableAmount(sellAmount, true, sellCoin)
                << "\tOther buys  " << PrintableAmount(getBuyAmount(), true, buyCoin);
        }
        */
    }

    const DexOrderID& DexOrder::getID() const
    {
        return orderID;
    }

    const WalletID& DexOrder::getSBBSID() const
    {
        return sbbsID;
    }

    Asset::ID DexOrder::getISendCoin() const
    {
        if (isMine)
        {
            throw std::runtime_error("should not create tx from own orders");
            return side == DexMarketSide::Sell ? market.first : market.second;
        }
        else
        {
            return side == DexMarketSide::Sell ? market.second : market.first;
        }
    }

    Asset::ID DexOrder::getIReceiveCoin() const
    {
        if (isMine)
        {
            throw std::runtime_error("should not create tx from own orders");
            return side == DexMarketSide::Sell ? market.second : market.first;
        }
        else
        {
            return side == DexMarketSide::Sell ? market.first : market.second;
        }
    }

    Amount DexOrder::getISendAmount() const
    {
        if (isMine)
        {
            throw std::runtime_error("should not create tx from own orders");
            return  side == DexMarketSide::Sell ? origSize : origSize * origPrice;
        }
        else
        {
             return  side == DexMarketSide::Sell ? origSize * origPrice : origSize;
        }
    }

    Amount DexOrder::getIReceiveAmount() const
    {
        if (isMine)
        {
            throw std::runtime_error("should not create tx from own orders");
            return side == DexMarketSide::Sell ? origSize * origPrice : origSize;
        }
        else
        {
            return side == DexMarketSide::Sell ? origSize : origSize * origPrice;
        }
    }

    DexMarketSide DexOrder::getSide() const
    {
        return side;
    }

    bool DexOrder::IsMine() const
    {
        return isMine;
    }

    Timestamp DexOrder::getExpiration() const
    {
        return expireTime;
    }

    ECC::Scalar::Native DexOrder::derivePrivateKey(beam::Key::IKdf::Ptr pkdf) const
    {
        if (!pkdf)
        {
            throw std::runtime_error("DexOrder::getPrivateKey - no KDF");
        }

        using PrivateKey = ECC::Scalar::Native;

        PrivateKey sk;
        pkdf->DeriveKey(sk, ECC::Key::ID(sbbsKeyIDX, Key::Type::Bbs));

        // Do not delete FromSk can change sk
        PeerID pubKey;
        pubKey.FromSk(sk);

        return sk;
    }

    PeerID DexOrder::derivePublicKey(beam::Key::IKdf::Ptr pkdf) const
    {
        auto privKey = derivePrivateKey(std::move(pkdf));

        PeerID pubKey;
        pubKey.FromSk(privKey);

        return pubKey;
    }

    Amount DexOrder::getPrice() const
    {
        return origPrice;
    }

    Amount DexOrder::getSize() const
    {
        return origSize;
    }
}
