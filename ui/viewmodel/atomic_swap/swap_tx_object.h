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
    SwapTxObject(const beam::wallet::TxDescription& tx, QObject* parent = nullptr);

    auto getSentAmount() const -> QString;
    auto getSentAmountValue() const -> beam::Amount;
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
    auto getFeeRate() const -> QString;

    bool isProofReceived() const;
    bool isBeamSideSwap() const;

signals:

private:
    auto getSwapAmountValue(bool sent) const -> beam::Amount;
    auto getSwapAmount(bool sent) const -> QString;

    boost::optional<bool> m_isBeamSide;
    boost::optional<beam::wallet::AtomicSwapCoin> m_swapCoin;
};
