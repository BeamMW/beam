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

#include "wallet/base_tx_builder.h"
#include "wallet/base_transaction.h"

#include "common.h"

namespace beam::wallet
{
    class SharedTxBuilder : public BaseTxBuilder
    {
    public:
        SharedTxBuilder(BaseTransaction& tx, SubTxID subTxID, Amount amount = 0, Amount fee = 0);

        void InitTx(bool isTxOwner);
        Transaction::Ptr CreateTransaction() override;
        Height GetMaxHeight() const override;

        bool GetSharedParameters();
        ECC::Point::Native GetPublicExcess() const override;

    protected:

        void InitInput();
        void InitOutput();
        void InitMinHeight();

        //void InitOffset();
        void LoadPeerOffset();


        ECC::Scalar::Native m_SharedBlindingFactor;
        ECC::Point::Native m_PeerPublicSharedBlindingFactor;
    };
}