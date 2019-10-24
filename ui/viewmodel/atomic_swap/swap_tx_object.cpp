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

#include "swap_tx_object.h"
#include "wallet/swaps/common.h"
#include "viewmodel/qml_globals.h"

using namespace beam;
using namespace beam::wallet;

SwapTxObject::SwapTxObject(QObject* parent)
        : TxObject(parent),
          m_isBeamSide(boost::none),
          m_swapCoin(boost::none)
{
}

SwapTxObject::SwapTxObject(const TxDescription& tx, QObject* parent/* = nullptr*/)
        : TxObject(tx, parent),
          m_isBeamSide(m_tx.GetParameter<bool>(TxParameterID::AtomicSwapIsBeamSide)),
          m_swapCoin(m_tx.GetParameter<AtomicSwapCoin>(TxParameterID::AtomicSwapCoin))
{
}

auto SwapTxObject::isBeamSideSwap() const -> bool
{
    if (m_isBeamSide)
    {
        return *m_isBeamSide;
    }
    else return false;
}

auto SwapTxObject::getSwapCoinName() const -> QString
{
    if (m_swapCoin)
    {
        switch (*m_swapCoin)
        {
            case AtomicSwapCoin::Bitcoin:   return toString(beamui::Currencies::Bitcoin);
            case AtomicSwapCoin::Litecoin:  return toString(beamui::Currencies::Litecoin);
            case AtomicSwapCoin::Qtum:      return toString(beamui::Currencies::Qtum);
            case AtomicSwapCoin::Unknown:   return toString(beamui::Currencies::Unknown);
        }
    }
    return QString("unknown");
}

QString SwapTxObject::getSentAmount() const
{
    if (m_type == TxType::AtomicSwap)
    {
        return getSwapAmount(true);
    }
    return m_tx.m_sender ? getAmount() : "";
}

beam::Amount SwapTxObject::getSentAmountValue() const
{
    if (m_type == TxType::AtomicSwap)
    {
        return getSwapAmountValue(true);
    }

    return m_tx.m_sender ? m_tx.m_amount : 0;
}

QString SwapTxObject::getReceivedAmount() const
{
    if (m_type == TxType::AtomicSwap)
    {
        return getSwapAmount(false);
    }
    return !m_tx.m_sender ? getAmount() : "";
}

beam::Amount SwapTxObject::getReceivedAmountValue() const
{
    if (m_type == TxType::AtomicSwap)
    {
        return getSwapAmountValue(false);
    }

    return !m_tx.m_sender ? m_tx.m_amount : 0;
}

QString SwapTxObject::getSwapAmount(bool sent) const
{
    if (!m_isBeamSide)
    {
        return "";
    }

    bool s = sent ? !*m_isBeamSide : *m_isBeamSide;
    if (s)
    {
        auto swapAmount = m_tx.GetParameter<Amount>(TxParameterID::AtomicSwapAmount);
        if (swapAmount)
        {
            return AmountToString(*swapAmount, beamui::convertSwapCoinToCurrency(*m_swapCoin));
        }
        return "";
    }
    return getAmount();
}

beam::Amount SwapTxObject::getSwapAmountValue(bool sent) const
{
    if (!m_isBeamSide)
    {
        return 0;
    }

    bool s = sent ? !*m_isBeamSide : *m_isBeamSide;
    if (s)
    {
        auto swapAmount = m_tx.GetParameter<Amount>(TxParameterID::AtomicSwapAmount);
        if (swapAmount)
        {
            return *swapAmount;
        }
        return 0;
    }
    return m_tx.m_amount;
}

QString SwapTxObject::getFeeRate() const
{
    auto feeRate = m_tx.GetParameter<beam::Amount>(TxParameterID::Fee, *m_isBeamSide ? SubTxIndex::REDEEM_TX : SubTxIndex::LOCK_TX);

    if (feeRate && m_swapCoin)
    {
        QString value = AmountToString(*feeRate, beamui::Currencies::Unknown);

        QString rateMeasure;
        switch (*m_swapCoin)
        {
        case AtomicSwapCoin::Bitcoin:
            rateMeasure = QMLGlobals::btcFeeRateLabel();
            break;

        case AtomicSwapCoin::Litecoin:
            rateMeasure = QMLGlobals::ltcFeeRateLabel();
            break;

        case AtomicSwapCoin::Qtum:
            rateMeasure = QMLGlobals::qtumFeeRateLabel();
            break;
        
        default:
            break;
        }
        return value + " " + rateMeasure;
    }
    return QString();
}

namespace
{
    template<typename T>
    void copyParameter(TxParameterID id, const TxParameters& source, TxParameters& dest)
    {
        if (auto p = source.GetParameter<T>(id); p)
        {
            dest.SetParameter(id, *p);
        }
    }

    void copyParameter(TxParameterID id, const TxParameters& source, TxParameters& dest, bool inverse = false)
    {
        if (auto p = source.GetParameter<bool>(id); p)
        {
            dest.SetParameter(id, inverse ? !*p : *p);
        }
    }

    template<size_t V>
    QString getSwapCoinTxId(const TxParameters& source)
    {
        if (auto res = source.GetParameter<std::string>(TxParameterID::AtomicSwapExternalTxID, V))
        {
            return QString(res->c_str());
        }
        else return QString();
    }
    
    template<size_t V>
    QString getSwapCoinTxConfirmations(const TxParameters& source)
    {
        if (auto res = source.GetParameter<uint32_t>(TxParameterID::Confirmations, V))
        {
            auto n = std::to_string(*res);
            return QString::fromStdString(n);
        }
        else return QString();
    }

    template<size_t V>
    QString getBeamTxKernelId(const TxParameters& source)
    {
        if (auto res = source.GetParameter<Merkle::Hash>(TxParameterID::KernelID, V))
        {
            return QString::fromStdString(to_hex(res->m_pData, res->nBytes));
        }
        else return QString();
    }
}

QString SwapTxObject::getToken() const
{
    if (m_type != TxType::AtomicSwap)
    {
        return "";
    }

    TxParameters tokenParams(m_tx.m_txId);

    auto isInitiator = m_tx.GetParameter<bool>(TxParameterID::IsInitiator);
    if (*isInitiator == false) 
    {
        if (auto p = m_tx.GetParameter<WalletID>(TxParameterID::MyID); p)
        {
            tokenParams.SetParameter(TxParameterID::PeerID, *p);
        }
    }
    else
    {
        copyParameter<WalletID>(TxParameterID::PeerID, m_tx, tokenParams);
    }

    tokenParams.SetParameter(TxParameterID::IsInitiator, true);

    copyParameter(TxParameterID::IsSender, m_tx, tokenParams, !*isInitiator);
    copyParameter(TxParameterID::AtomicSwapIsBeamSide, m_tx, tokenParams, !*isInitiator);

    tokenParams.SetParameter(beam::wallet::TxParameterID::TransactionType, m_type);
    copyParameter<Height>(TxParameterID::MinHeight, m_tx, tokenParams);
    copyParameter<Height>(TxParameterID::PeerResponseTime, m_tx, tokenParams);
    copyParameter<Timestamp>(TxParameterID::CreateTime, m_tx, tokenParams);
    copyParameter<Height>(TxParameterID::Lifetime, m_tx, tokenParams);

    copyParameter<Amount>(TxParameterID::Amount, m_tx, tokenParams);
    copyParameter<Amount>(TxParameterID::AtomicSwapAmount, m_tx, tokenParams);
    copyParameter<AtomicSwapCoin>(TxParameterID::AtomicSwapCoin, m_tx, tokenParams);

    return QString::fromStdString(std::to_string(tokenParams));
}

bool SwapTxObject::isProofReceived() const
{
    Height proofHeight;
    if (m_tx.GetParameter(TxParameterID::KernelProofHeight, proofHeight, SubTxIndex::BEAM_LOCK_TX))
    {
        return true;
    }
    else return false;
}

QString SwapTxObject::getSwapCoinLockTxId() const
{
    return getSwapCoinTxId<SubTxIndex::LOCK_TX>(m_tx);
}

QString SwapTxObject::getSwapCoinRedeemTxId() const
{
    return getSwapCoinTxId<SubTxIndex::REDEEM_TX>(m_tx);
}

QString SwapTxObject::getSwapCoinRefundTxId() const
{
    return getSwapCoinTxId<SubTxIndex::REFUND_TX>(m_tx);
}

QString SwapTxObject::getSwapCoinLockTxConfirmations() const
{
    return getSwapCoinTxConfirmations<SubTxIndex::LOCK_TX>(m_tx);
}

QString SwapTxObject::getSwapCoinRedeemTxConfirmations() const
{
    return getSwapCoinTxConfirmations<SubTxIndex::REDEEM_TX>(m_tx);
}

QString SwapTxObject::getSwapCoinRefundTxConfirmations() const
{
    return getSwapCoinTxConfirmations<SubTxIndex::REFUND_TX>(m_tx);
}

QString SwapTxObject::getBeamLockTxKernelId() const
{
    return getBeamTxKernelId<SubTxIndex::BEAM_LOCK_TX>(m_tx);
}

QString SwapTxObject::getBeamRedeemTxKernelId() const
{
    return getBeamTxKernelId<SubTxIndex::REDEEM_TX>(m_tx);
}

QString SwapTxObject::getBeamRefundTxKernelId() const
{
    return getBeamTxKernelId<SubTxIndex::REFUND_TX>(m_tx);
}
