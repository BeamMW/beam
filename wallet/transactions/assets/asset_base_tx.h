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

#include "wallet/core/common.h"
#include "wallet/core/wallet_db.h"
#include "wallet/core/base_transaction.h"
#include "wallet/core/base_tx_builder.h"

namespace beam::wallet {
    class AssetTransaction : public BaseTransaction
    {
    protected:
        AssetTransaction(const TxType txType, const TxContext& context);

        bool Rollback(Height height) override;
        bool BaseUpdate();
        bool IsLoopbackTransaction() const;

        struct Builder
            :public BaseTxBuilder
        {
            Builder(BaseTransaction& tx, SubTxID subTxID);

            Asset::Metadata m_Md;
            ECC::Scalar::Native m_skAsset;
            PeerID m_pidAsset;

            void FinalyzeTxInternal() override; // also signs the kernel
        };
    };
}
