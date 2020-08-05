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
    TxParameters CreatePushTransactionParameters(const WalletID& myID, const boost::optional<TxID>& txId = boost::none);

    class PushTxBuilder;

    class PushTransaction : public BaseTransaction
    {
    public:
        class Creator : public BaseTransaction::Creator
        {
        public:
            Creator(IWalletDB::Ptr walletDB) 
                : m_walletDB(walletDB)
                {}

        private:
            BaseTransaction::Ptr Create(const TxContext& context) override;

            TxParameters CheckAndCompleteParameters(const TxParameters& parameters) override;
            IWalletDB::Ptr m_walletDB;
        };

    public:
        PushTransaction(const TxContext& context);

    private:
        TxType GetType() const override;
        bool IsInSafety() const override;
        void UpdateImpl() override;
        void RollbackTx() override;
    private:
        std::shared_ptr<PushTxBuilder> m_TxBuilder;
        TxoID m_OutpID = 0;
        Height m_OutpHeight = 0;

        void OnOutpProof(proto::ProofShieldedOutp&);
    };
} // namespace beam::wallet::lelantus