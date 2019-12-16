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

#include "swap_tx_object.h"
#include "viewmodel/helpers/list_model.h"

class SwapTxObjectList : public ListModel<std::shared_ptr<SwapTxObject>>
{

    Q_OBJECT

public:
    enum class Roles
    {
        TimeCreated = Qt::UserRole + 1,
        TimeCreatedSort,
        AmountGeneralWithCurrency,
        AmountGeneralWithCurrencySort,
        AmountGeneral,
        AmountGeneralSort,
        AddressFrom,
        AddressFromSort,
        AddressTo,
        AddressToSort,
        Status,
        StatusSort,
        Fee,
        Comment,
        TxID,
        KernelID,
        FailureReason,
        IsCancelAvailable,
        IsDeleteAvailable,
        IsSelfTransaction,
        IsIncome,
        IsInProgress,
        IsPending,
        IsCompleted,
        IsCanceled,
        IsFailed,
        IsExpired,
        HasPaymentProof,
        RawTxID,
        Search,
        // atomic swap only roles
        IsBeamSideSwap,
        IsLockTxProofReceived,
        IsRefundTxProofReceived,
        AmountSendWithCurrency,
        AmountSendWithCurrencySort,
        AmountSend,
        AmountSendSort,
        AmountReceiveWithCurrency,
        AmountReceiveWithCurrencySort,
        AmountReceive,
        AmountReceiveSort,
        Token,
        SwapCoin,
        SwapCoinFeeRate,
        SwapCoinFee,
        SwapCoinLockTxId,
        SwapCoinLockTxConfirmations,
        SwapCoinRedeemTxId,
        SwapCoinRedeemTxConfirmations,
        SwapCoinRefundTxId,
        SwapCoinRefundTxConfirmations,
        BeamLockTxKernelId,
        BeamRedeemTxKernelId,
        BeamRefundTxKernelId,
        StateDetails
    };

    SwapTxObjectList();

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void remove(const std::vector<std::shared_ptr<SwapTxObject>>& items);
    void update(const std::vector<std::shared_ptr<SwapTxObject>>& items);
};
