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

#include "wallet/core/base_tx_builder.h"

namespace beam::wallet::lelantus
{
    class BaseLelantusTxBuilder : public BaseTxBuilder
    {
    public:
        BaseLelantusTxBuilder(BaseTransaction& tx, const AmountList& amount, Amount fee, bool withAssets);

        bool GetInitialTxParams() override;
        Height GetMaxHeight() const override;

    protected:
        static void Restore(ShieldedTxo::DataParams&, const ShieldedCoin&, const ShieldedTxo::Viewer&);
        bool m_withAssets;
    };
} // namespace beam::wallet::lelantus