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

#include "common.h"
#include "swap_transaction.h"

namespace beam::wallet
{
    std::string GetSwapTxStatusStr(const TxParameters& txParams);

    struct SwapTxDescription
    {
        SwapTxDescription(const TxParameters&);

        // mandatory swap txransaction parameters
        AtomicSwapCoin getSwapCoin() const;
        Amount getSwapAmount() const;
        Height getMinHeight() const;
        Height getResponseTime() const;
        // optional parameters
        boost::optional<std::string> getToken() const;
        boost::optional<Amount> getFee() const;
        boost::optional<Amount> getSwapCoinFeeRate() const;
        boost::optional<Height> getMinRefundTxHeight() const;
        boost::optional<Height> getMaxLockTxHeight() const;
        boost::optional<Height> getExternalHeight() const;
        boost::optional<Height> getExternalLockTime() const;
        boost::optional<TxFailureReason> getFailureReason() const;
        boost::optional<AtomicSwapTransaction::State> getState() const;

        bool isBeamSide() const;
        bool isFailed() const;
        bool isExpired() const;
        bool isRefunded() const;
        bool isCancelAvailable() const;

        bool isRedeemTxRegistered() const;
        bool isRefundTxRegistered() const;
        bool isLockTxProofReceived() const;
        bool isRefundTxProofReceived() const;

        template<SubTxIndex SubTxId>
        boost::optional<std::string> getSwapCoinTxId() const
        {
            return m_tx.GetParameter<std::string>(TxParameterID::AtomicSwapExternalTxID, SubTxId);
        };

        template<SubTxIndex SubTxId>
        boost::optional<uint32_t> getSwapCoinTxConfirmations() const
        {
            return m_tx.GetParameter<uint32_t>(TxParameterID::Confirmations, SubTxId);
        };

        template<SubTxIndex SubTxId>
        boost::optional<std::string> getBeamTxKernelId() const
        {
            if (auto res = m_tx.GetParameter<Merkle::Hash>(TxParameterID::KernelID, SubTxId); res)
            {
                return beam::to_hex(res->m_pData, res->nBytes);
            }
            return boost::none;
        };

    private:
        const TxParameters m_tx;
        bool m_isBeamSide;
        TxStatus m_status;
    };
}   // namespace beam::wallet
