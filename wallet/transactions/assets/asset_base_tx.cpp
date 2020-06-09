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
#include "asset_base_tx.h"

// TODO:ASSETS move what you can to base class
namespace beam::wallet {
    AssetTransaction::AssetTransaction(const TxContext& context)
        : BaseTransaction(context)
    {
    }

    bool AssetTransaction::Rollback(Height height)
    {
        bool rthis = false;

        Height cheight = 0;
        if (GetParameter(TxParameterID::AssetConfirmedHeight, cheight) && (cheight > height))
        {
            SetParameter(TxParameterID::AssetConfirmedHeight, Height(0));
            SetParameter(TxParameterID::AssetInfoFull, Asset::Full());
            rthis = true;
        }

        Height uheight = 0;
        if (GetParameter(TxParameterID::AssetUnconfirmedHeight, uheight) && (uheight > height))
        {
            SetParameter(TxParameterID::AssetUnconfirmedHeight, Height(0));
            rthis = true;
        }

        const auto rsuper = BaseTransaction::Rollback(height);
        return rthis || rsuper;
    }

    bool AssetTransaction::BaseUpdate()
    {
        Height minHeight = 0;
        if (!GetParameter(TxParameterID::MinHeight, minHeight))
        {
            minHeight = GetWalletDB()->getCurrentHeight();
            SetParameter(TxParameterID::MinHeight, minHeight);
        }

        Height maxHeight = 0;
        if (!GetParameter(TxParameterID::MaxHeight, maxHeight))
        {
            Height lifetime = kDefaultTxLifetime;
            GetParameter(TxParameterID::Lifetime, lifetime);

            maxHeight = minHeight + lifetime;
            SetParameter(TxParameterID::MaxHeight, maxHeight);
        }

        if (!IsLoopbackTransaction())
        {
            OnFailed(TxFailureReason::NotLoopback, true);
            return false;
        }

        if (CheckExpired())
        {
            return false;
        }

        return true;
    }

    bool AssetTransaction::IsLoopbackTransaction() const
    {
        return GetMandatoryParameter<bool>(TxParameterID::IsSender) && IsInitiator();
    }
}