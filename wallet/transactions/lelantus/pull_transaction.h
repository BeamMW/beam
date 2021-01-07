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

#include "wallet/core/base_transaction.h"
#include "wallet/core/base_tx_builder.h"

namespace beam::wallet::lelantus
{
    TxParameters CreatePullTransactionParameters(const WalletID& myID, const boost::optional<TxID>& txId = boost::none);

    class PullTxBuilder;

    class PullTransaction : public BaseTransaction
    {
    public:
        class Creator : public BaseTransaction::Creator
        {
            BaseTransaction::Ptr Create(const TxContext& context) override;

            TxParameters CheckAndCompleteParameters(const TxParameters& parameters) override;
        };

        enum State : uint8_t
        {
            Initial,
            Registration,
            KernelConfirmation,
        };

    public:
        PullTransaction(const TxContext& context);

    private:
        bool IsInSafety() const override;
        void UpdateImpl() override;
        void RollbackTx() override;

    private:
        struct MyBuilder;
        std::shared_ptr<MyBuilder> m_TxBuilder;
    };
} // namespace beam::wallet::lelantus