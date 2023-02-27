// Copyright 2018 The Beam Team
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
#include "wallet_db.h"
#include "base_transaction.h"

namespace beam::wallet
{
    class ContractTransaction : public BaseTransaction
    {
    public:
        enum State : uint8_t
        {
            Initial,
            GeneratingCoins,
            Registration,
            OutputsConfirmation,
            Negotiating,
            Recreation,
        };

        class Creator : public BaseTransaction::Creator
        {
            BaseTransaction::Ptr Create(const TxContext& context) override;
            IWalletDB::Ptr m_WalletDB;
        public:
            Creator(IWalletDB::Ptr walletDB);
        };

    private:
        ContractTransaction(const TxContext& context);

    private:
        bool IsInSafety() const override;
        void UpdateImpl() override;
        bool CheckExpired() override;
        bool CanCancel() const override;

        void Init();
        void BuildTxOnce();
        int RegisterTx();
        bool IsExpired(Height hTrg);

    private:
        struct MyBuilder;
        std::shared_ptr<MyBuilder> m_TxBuilder;
    };
}
