
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

namespace {
    const char kBeamAssetSName[] = "BEAM";
    const char kUnknownAssetSName[] = "UNKNOWN";
}

namespace beam::wallet
{

DexOrder::DexOrder(const ByteBuffer& buffer, const ByteBuffer& signature)
{
    if (!fromByteBuffer(buffer, *this))
    {
        throw std::runtime_error("DexOrder::DexOrder failed to parse order body");
    }

    if (_version != DexOrder::getCurrentVersion())
    {
        throw std::runtime_error("DexOrder::DexOrder obsolete order version");
    }

    SignatureHandler sig;
    sig.m_data = buffer;

    if (!fromByteBuffer(signature, sig.m_Signature))
    {
        throw std::runtime_error("DexOrder::DexOrder failed to parse signature");
    }

    if (!sig.IsValid(_sbbsID.m_Pk))
    {
        throw std::runtime_error("DexOrder::DexOrder failed to verify signature");
    }
}

DexOrder::DexOrder(const ByteBuffer& buffer, bool isMine)
{
    if (!fromByteBuffer(buffer, *this))
    {
        throw std::runtime_error("DexOrder::DexOrder failed to parse order body");
    }

    if (_version != DexOrder::getCurrentVersion())
    {
        throw std::runtime_error("DexOrder::DexOrder obsolete order version");
    }

    _isMine = isMine;
}

DexOrder::DexOrder(DexOrderID    orderId,
                   WalletID      sbbsId,
                   uint64_t      sbbsKeyIdx,
                   Asset::ID     assetIdFirst,
                   Amount        assetAmountFirst,
                   std::string   assetSnameFirst,
                   Asset::ID     assetIdSecond,
                   Amount        assetAmountSecond,
                   std::string   assetSnameSecond,
                   uint32_t      expiration)
    : _version(kCurrentOfferVer)
    , _orderID(orderId)
    , _sbbsID(sbbsId)
    , _sbbsKeyIDX(sbbsKeyIdx)
    , _assetIdFirst(assetIdFirst)
    , _assetIdSecond(assetIdSecond)
    , _assetSnameFirst(assetSnameFirst)
    , _assetSnameSecond(assetSnameSecond)
    , _assetAmountFirst(assetAmountFirst)
    , _assetAmountSecond(assetAmountSecond)
    , _isMine(true)
    , _createTime(getTimestamp())
    , _expireTime(_createTime + expiration)
{
}

uint32_t DexOrder::getVersion() const
{
    return _version;
}

bool DexOrder::isExpired() const
{
    const auto now = getTimestamp();
    return _expireTime < now;
}

const DexOrderID& DexOrder::getID() const
{
    return _orderID;
}

const WalletID& DexOrder::getSBBSID() const
{
    return _sbbsID;
}

bool DexOrder::isMine() const
{
    return _isMine;
}

Timestamp DexOrder::getCreation() const
{
    return _createTime;
}

Timestamp DexOrder::getExpiration() const
{
    return _expireTime;
}

Amount DexOrder::getFirstAmount() const
{
    return _assetAmountFirst;
}

Amount DexOrder::getSecondAmount() const
{
    return _assetAmountSecond;
}

Asset::ID DexOrder::getFirstAssetId() const
{
    return _assetIdFirst;
}

Asset::ID DexOrder::getSecondAssetId() const
{
    return _assetIdSecond;
}

std::string DexOrder::getFirstAssetSname() const
{
    return _assetSnameFirst.empty() ? (_assetIdFirst == Asset::s_BeamID ? kBeamAssetSName : kUnknownAssetSName) : _assetSnameFirst;
}

std::string DexOrder::getSecondAssetSname() const
{
    return _assetSnameSecond.empty() ? (_assetIdSecond == Asset::s_BeamID ? kBeamAssetSName : kUnknownAssetSName) : _assetSnameSecond;
}

bool DexOrder::isCanceled() const
{
    return _isCanceled;
}

Amount DexOrder::getSendAmount() const
{
    return isMine() ? getFirstAmount() : getSecondAmount();
}

Amount DexOrder::getReceiveAmount() const
{
    return isMine() ? getSecondAmount() : getFirstAmount();
}

Asset::ID DexOrder::getSendAssetId() const
{
    return isMine() ? getFirstAssetId() : getSecondAssetId();
}

Asset::ID DexOrder::getReceiveAssetId() const
{
    return isMine() ? getSecondAssetId() : getFirstAssetId();
}

std::string DexOrder::getSendAssetSName() const
{
    return isMine() ? getFirstAssetSname() : getSecondAssetSname();
}

std::string DexOrder::getReceiveAssetSName() const
{
    return isMine() ? getSecondAssetSname() : getFirstAssetSname();
}

void DexOrder::cancel()
{
    _isCanceled = true;
}

bool DexOrder::isAccepted() const
{
    return _isAccepted;
}

void DexOrder::setAccepted(bool value)
{
    if (_isMine)
        _isAccepted = value;
}

void DexOrder::setIsMine(bool value)
{
    _isMine = value;
}

uint64_t DexOrder::get_KeyID() const
{
    return _sbbsKeyIDX;
}

}  // namespace beam::wallet
