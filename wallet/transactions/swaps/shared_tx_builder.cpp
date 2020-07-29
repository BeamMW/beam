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


#include "shared_tx_builder.h"

using namespace ECC;

namespace beam::wallet
{
    namespace
    {
        AmountList ConstructAmountList(Amount amount)
        {
            AmountList res;
            if (amount > 0)
            {
                res.push_back(amount);
            }
            return res;
        }
    }

    SharedTxBuilder::SharedTxBuilder(BaseTransaction& tx, SubTxID subTxID, Amount amount, Amount fee)
        : MutualTxBuilder(tx, subTxID, ConstructAmountList(amount), fee)
    {
    }

    Transaction::Ptr SharedTxBuilder::CreateTransaction()
    {
        LoadPeerOffset();
        return MutualTxBuilder::CreateTransaction();
    }

    Height SharedTxBuilder::GetMaxHeight() const
    {
        return m_Height.m_Max;
    }

    bool SharedTxBuilder::GetSharedParameters()
    {
        return m_Tx.GetParameter(TxParameterID::SharedBlindingFactor, m_SharedBlindingFactor, SubTxIndex::BEAM_LOCK_TX)
            && m_Tx.GetParameter(TxParameterID::PeerPublicSharedBlindingFactor, m_PeerPublicSharedBlindingFactor, SubTxIndex::BEAM_LOCK_TX);
    }


    void SharedTxBuilder::InitTx(bool isTxOwner)
    {
        InitInput();

        if (isTxOwner)
            // select shared UTXO as input and create output utxo
            InitOutput();

        m_GeneratingInOuts = Stage::Done;
    }

    void SharedTxBuilder::InitInput()
    {
        // load shared utxo as input
        Point::Native commitment(Zero);
        Amount amount = m_Tx.GetMandatoryParameter<Amount>(TxParameterID::Amount);
        Tag::AddValue(commitment, nullptr, amount);
        commitment += Context::get().G * m_SharedBlindingFactor;
        commitment += m_PeerPublicSharedBlindingFactor;

        auto& input = m_pTransaction->m_vInputs.emplace_back(std::make_unique<Input>());
        input->m_Commitment = commitment;
        m_Tx.SetParameter(TxParameterID::Inputs, m_pTransaction->m_vInputs, false, m_SubTxID);

    }

    void SharedTxBuilder::InitOutput()
    {
        Coin outputCoin;

        if (!m_Tx.GetParameter(TxParameterID::SharedCoinID, outputCoin.m_ID, m_SubTxID))
        {
            outputCoin = m_Tx.GetWalletDB()->generateNewCoin(GetAmount(), Zero);
            m_Tx.SetParameter(TxParameterID::SharedCoinID, outputCoin.m_ID, m_SubTxID);

            if (m_SubTxID == SubTxIndex::BEAM_REDEEM_TX)
            {
                outputCoin.m_createTxId = m_Tx.GetTxID();
                m_Tx.GetWalletDB()->saveCoin(outputCoin);
            }
        }

		Height minHeight = 0;
		m_Tx.GetParameter(TxParameterID::MinHeight, minHeight, m_SubTxID);

        // add output
        IPrivateKeyKeeper2::Method::CreateOutput m;
        m.m_hScheme = minHeight;
        m.m_Cid = outputCoin.m_ID;

        m_Tx.TestKeyKeeperRet(m_Tx.get_KeyKeeperStrict()->InvokeSync(m));

        m_pTransaction->m_vOutputs.push_back(std::move(m.m_pResult));
        m_Coins.m_Output.push_back(outputCoin.m_ID);
        m_Tx.SetParameter(TxParameterID::Outputs, m_pTransaction->m_vOutputs, m_SubTxID);
    }

    void SharedTxBuilder::LoadPeerOffset()
    {
        m_Tx.GetParameter(TxParameterID::PeerOffset, m_PeerOffset, m_SubTxID);
    }
}