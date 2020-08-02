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

#include "common.h"

namespace beam::wallet
{

    class SharedTxBuilder
        :public MutualTxBuilder
    {
    public:
        SharedTxBuilder(BaseTransaction& tx, SubTxID subTxID);

        bool IsRedeem() const {
            return SubTxIndex::BEAM_REDEEM_TX == m_SubTxID;
        }

        bool AddSharedOffset();
        bool AddSharedInput();

        struct Status
            :public MutualTxBuilder::Status
        {
            static const Type SndSig2Sent = 10;
            static const Type RcvSig2Received = 10;
        };

    protected:

        bool IsConventional() override { return false; }
        void SendToPeer(SetTxParameter&& msgSub) override;
        bool SignTxSender() override;
        bool SignTxReceiver() override;
        void FinalyzeTxInternal() override;
    };



}