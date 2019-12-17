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

#include "viewmodel/wallet/tx_object.h"

class SwapTxObject : public TxObject
{
    Q_OBJECT

public:
    SwapTxObject(QObject* parent = nullptr);
    SwapTxObject(const beam::wallet::TxDescription& tx, uint32_t minTxConfirmations, double blocksPerHour, QObject* parent = nullptr);

    auto getSentAmountWithCurrency() const -> QString;
    auto getSentAmount() const-> QString;
    auto getSentAmountValue() const -> beam::Amount;
    auto getReceivedAmountWithCurrency() const-> QString;
    auto getReceivedAmount() const -> QString;
    auto getReceivedAmountValue() const -> beam::Amount;
    auto getToken() const -> QString;
    auto getSwapCoinLockTxId() const -> QString;
    auto getSwapCoinLockTxConfirmations() const -> QString;
    auto getSwapCoinRedeemTxId() const -> QString;
    auto getSwapCoinRedeemTxConfirmations() const -> QString;
    auto getSwapCoinRefundTxId() const -> QString;
    auto getSwapCoinRefundTxConfirmations() const -> QString;
    auto getBeamLockTxKernelId() const -> QString;
    auto getBeamRedeemTxKernelId() const -> QString;
    auto getBeamRefundTxKernelId() const -> QString;
    auto getSwapCoinName() const -> QString;
    auto getSwapCoinFeeRate() const -> QString;
    auto getSwapCoinFee() const -> QString;
    auto getFee() const -> QString override;
    auto getStatus() const -> QString override;
    auto getFailureReason() const -> QString override;
    QString getStateDetails() const override;
    beam::wallet::AtomicSwapCoin getSwapCoinType() const;

    bool isLockTxProofReceived() const;
    bool isRefundTxProofReceived() const;
    bool isBeamSideSwap() const;
    
    bool isCancelAvailable() const override;
    bool isDeleteAvailable() const override;
    bool isInProgress() const override;
    bool isPending() const override;
    bool isExpired() const override;
    bool isCompleted() const override;
    bool isCanceled() const override;
    bool isFailed() const override;

signals:

private:
    auto getSwapAmountValue(bool sent) const -> beam::Amount;
    auto getSwapAmountWithCurrency(bool sent) const -> QString;

    boost::optional<bool> m_isBeamSide;
    boost::optional<beam::wallet::AtomicSwapCoin> m_swapCoin;
    uint32_t m_minTxConfirmations = 0;
    double m_blocksPerHour = 0;
};
