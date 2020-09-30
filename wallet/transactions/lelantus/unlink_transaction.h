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

namespace beam::wallet::lelantus
{
    TxParameters CreateUnlinkFundsTransactionParameters(const WalletID& myID, const boost::optional<TxID>& txId = boost::none);

    class PushTransaction;
    class PullTransaction;

    class UnlinkFundsTransaction : public BaseTransaction
    {
    public:
        class Creator : public BaseTransaction::Creator
        {
            BaseTransaction::Ptr Create(const TxContext& context) override;

            TxParameters CheckAndCompleteParameters(const TxParameters& parameters) override;
        };

    public:
        UnlinkFundsTransaction(const TxContext& context);

    private:

        enum struct State : uint8_t
        {
            Initial,
            Insertion,
            Unlinking,
            Extraction,
            Cancellation,
            BeforeExtraction
        };

        struct SubTxIndex
        {
            static constexpr SubTxID PUSH_TX = 2;
            static constexpr SubTxID PULL_TX = 3;
        };

        TxType GetType() const override;
        bool Rollback(Height height) override;
        void Cancel() override;
        bool IsInSafety() const override;
        void RollbackTx() override;
        void UpdateImpl() override;

        State GetState() const;
        void UpdateActiveTransactions();
        void CreateInsertTransaction();
        bool CheckAnonymitySet() const;
        void CreateExtractTransaction();
    private:
        BaseTransaction::Ptr m_ActiveTransaction;
        std::unique_ptr<INegotiatorGateway> m_ActiveGateway;
    };
} // namespace beam::wallet::lelantus