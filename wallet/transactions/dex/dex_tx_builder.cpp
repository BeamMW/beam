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
#include "dex_tx_builder.h"

namespace beam::wallet
{
    DexSimpleSwapBuilder::DexSimpleSwapBuilder(BaseTransaction& tx)
        : MutualTxBuilder(tx, kDefaultSubTxID)
    {
        // TODO:DEX CHECK, set in simple tx builder
        //m_Lifetime = 0; // disable auto max height adjustment

        GetParameter(TxParameterID::DexReceiveAsset, m_ReceiveAssetID);
        GetParameter(TxParameterID::DexReceiveAmount, m_ReceiveAmount);
        GetParameter(TxParameterID::DexOrderID, m_orderID);
    }

    void DexSimpleSwapBuilder::SendToPeer(SetTxParameter&& msg)
    {
        // TODO:DEX Check what's going on here
        Height hMax = (m_Height.m_Max != MaxHeight) ?
            m_Height.m_Max :
            m_Height.m_Min + m_Lifetime;

        // common parameters
        msg
            .AddParameter(TxParameterID::PeerProtoVersion, BaseTransaction::s_ProtoVersion) // why&
            .AddParameter(TxParameterID::PeerMaxHeight, hMax);

        if (m_IsSender)
        {
            // Here we swap receive & send amounts
            // It will reduce code for inputs & outputs creation,
            // it will be the same for sender & receiver
            msg
                .AddParameter(TxParameterID::Amount, m_ReceiveAmount)
                .AddParameter(TxParameterID::AssetID, m_ReceiveAssetID)
                .AddParameter(TxParameterID::DexReceiveAsset, m_AssetID)
                .AddParameter(TxParameterID::DexReceiveAmount, m_Amount)
                .AddParameter(TxParameterID::ExternalDexOrderID, m_orderID)
                .AddParameter(TxParameterID::Fee, m_Fee)
                .AddParameter(TxParameterID::MinHeight, m_Height.m_Min)
                .AddParameter(TxParameterID::Lifetime, m_Lifetime)
                .AddParameter(TxParameterID::IsSender, false);
        }
        else
        {
            LOG_INFO() << m_Tx.GetTxID() << " Transaction accepted. Kernel: " << GetKernelIDString();
        }

        m_Tx.SendTxParametersStrict(std::move(msg));
    }
}
