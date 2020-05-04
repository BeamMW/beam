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

#include "base_lelantus_tx_builder.h"
#include "core/shielded.h"

namespace beam::wallet::lelantus
{
    BaseLelantusTxBuilder::BaseLelantusTxBuilder(BaseTransaction& tx, const AmountList& amount, Amount fee)
        : BaseTxBuilder(tx, kDefaultSubTxID, amount, fee)
    {
    }

    bool BaseLelantusTxBuilder::GetInitialTxParams()
    {
        bool result = BaseTxBuilder::GetInitialTxParams();

        // Initialize MaxHeight, MaxHeight = MinHeight + Lifetime
        if (Height maxHeight = MaxHeight; !m_Tx.GetParameter(TxParameterID::MaxHeight, maxHeight))
        {
            maxHeight = GetMinHeight() + GetLifetime();
            m_Tx.SetParameter(TxParameterID::MaxHeight, maxHeight);
        }

        return result;
    }
   
    Height BaseLelantusTxBuilder::GetMaxHeight() const
    {
        return m_Tx.GetMandatoryParameter<Height>(TxParameterID::MaxHeight, m_SubTxID);
    }

    void BaseLelantusTxBuilder::Restore(ShieldedTxo::DataParams& sdp, const ShieldedCoin& sc, const ShieldedTxo::Viewer& viewer)
    {
        sdp.m_Serial.m_pK[0] = sc.m_skSerialG;
        sdp.m_Serial.m_IsCreatedByViewer = sc.m_isCreatedByViewer;
        sdp.m_Serial.Restore(viewer);

        sdp.m_Output.m_Value = sc.m_value;
        sdp.m_Output.m_AssetID = sc.m_assetID;
        sdp.m_Output.m_User = sc.m_User;

        sdp.m_Output.Restore_kG(sdp.m_Serial.m_SharedSecret);
    }

} // namespace beam::wallet::lelantus