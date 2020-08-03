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

#include "wallet/core/base_tx_builder.h"
#include "wallet/core/base_transaction.h"

namespace beam::wallet
{
    class LockTxBuilder
        :public MutualTxBuilder
    {
    public:
        LockTxBuilder(BaseTransaction& tx, Amount amount);


    private:

        bool IsConventional() override { return false; }
        void SendToPeer(SetTxParameter&&) override;
        void FinalyzeTxInternal() override;

        ECC::Scalar::Native m_Sk; // blinding factor
        ECC::Point::Native m_PubKeyN;
        ECC::Point m_PubKey;
        ECC::NoLeak<ECC::uintBig> m_SeedSk;

        void EvaluateSelfFields();
        void EvaluateOutput(Output&, ECC::RangeProof::Confidential& proof);
    };
}