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

#include "pull_tx_builder.h"

#include "core/shielded.h"
#include "utility/executor.h"

#include <random>

namespace beam::wallet::lelantus
{
    PullTxBuilder::PullTxBuilder(BaseTransaction& tx, const AmountList& amount, Amount fee, bool withAssets)
        : BaseLelantusTxBuilder(tx, amount, fee, withAssets)
    {
    }

    bool PullTxBuilder::GetShieldedList()
    {
        if (m_shieldedList.empty())
        {
            uint32_t windowSize = Rules::get().Shielded.m_ProofMax.get_N();
            TxoID windowBegin = 0;

            if (!m_Tx.GetParameter(TxParameterID::WindowBegin, windowBegin))
            {
                TxoID shieldedId = m_Tx.GetMandatoryParameter<TxoID>(TxParameterID::ShieldedOutputId);
                windowBegin = GenerateWindowBegin(shieldedId, windowSize);
                // update windowBegin
                m_Tx.SetParameter(TxParameterID::WindowBegin, windowBegin);
            }

            m_Tx.GetGateway().get_shielded_list(m_Tx.GetTxID(), windowBegin, windowSize, 
                [this, weak = this->weak_from_this(), weakTx = m_Tx.weak_from_this()](TxoID, uint32_t, proto::ShieldedList& msg)
            {
                if (!weak.expired() && !weakTx.expired())
                {
                    m_shieldedList.swap(msg.m_Items);
                    m_totalShieldedOuts = msg.m_ShieldedOuts;
                    m_Tx.UpdateAsync();
                }
            });
            return true;
        }
        return false;
    }

    Transaction::Ptr PullTxBuilder::CreateTransaction()
    {
        Key::IKdf::Ptr pMaster = m_Tx.get_MasterKdfStrict();

        auto shieldedId = m_Tx.GetMandatoryParameter<TxoID>(TxParameterID::ShieldedOutputId);
        auto startIndex = m_Tx.GetMandatoryParameter<TxoID>(TxParameterID::WindowBegin);

        // create transaction
        auto transaction = std::make_shared<Transaction>();
        transaction->m_vOutputs = move(m_Outputs);
        if (IsAssetTx())
        {
            transaction->m_vInputs = m_Tx.GetMandatoryParameter<std::vector<Input::Ptr>>(TxParameterID::Inputs);
        }

        ECC::Point::Native hGen;
        {
            ECC::Scalar::Native offset = Zero;

            for (auto id : GetInputCoins())
            {
                ECC::Scalar::Native inputSk;
                CoinID::Worker(id).Create(inputSk, *id.get_ChildKdf(pMaster));
                offset += inputSk;
            }

            for (const auto& id : m_OutputCoins)
            {
                ECC::Scalar::Native outputSk;
                CoinID::Worker(id).Create(outputSk, *id.get_ChildKdf(pMaster));
                offset -= outputSk;
                hGen = CoinID::Worker(id).m_hGen;
            }

            transaction->m_Offset = offset;
        }

        {
            Lelantus::Cfg cfg = Rules::get().Shielded.m_ProofMax;
            uint32_t windowSize = cfg.get_N();
            TxoID windowEnd = startIndex + m_shieldedList.size();
            bool isRestrictedMode = m_totalShieldedOuts > windowEnd + Rules::get().Shielded.MaxWindowBacklog;

            if (isRestrictedMode)
            {
                // TODO: find better solution
                // load Cfg for restricted mode
                cfg = Rules::get().Shielded.m_ProofMin;

                // update parameters
                windowSize = cfg.get_N();
                windowEnd = startIndex + windowSize;
            }

            TxKernelShieldedInput::Ptr pKrn(new TxKernelShieldedInput);
            pKrn->m_Fee = GetFee();
            pKrn->m_Height.m_Min = GetMinHeight();
            pKrn->m_Height.m_Max = GetMaxHeight();
            pKrn->m_WindowEnd = windowEnd;
            pKrn->m_SpendProof.m_Cfg = cfg;

            Lelantus::CmListVec lst;

            //assert(nWnd1 <= m_Shielded.m_Wnd0 + m_Shielded.m_N);
            if (isRestrictedMode)
            {
                lst.m_vec.resize(windowSize);
                std::copy_n(m_shieldedList.begin(), windowSize, lst.m_vec.begin());
            }
            else if (windowEnd == startIndex + windowSize)
            {
                lst.m_vec.resize(m_shieldedList.size());
                std::copy(m_shieldedList.begin(), m_shieldedList.end(), lst.m_vec.begin());
            }
            else
            {
                // zero-pad from left
                lst.m_vec.resize(windowSize);
                for (size_t i = 0; i < windowSize - m_shieldedList.size(); i++)
                {
                    ECC::Point::Storage& v = lst.m_vec[i];
                    v.m_X = Zero;
                    v.m_Y = Zero;
                }
                std::copy(m_shieldedList.begin(), m_shieldedList.end(), lst.m_vec.end() - m_shieldedList.size());
            }

            assert(shieldedId < windowEnd && shieldedId >= startIndex);
            uint32_t shieldedWindowId = 0;
            if (isRestrictedMode)
            {
                shieldedWindowId = static_cast<uint32_t>(shieldedId - startIndex);
            }
            else
            {
                shieldedWindowId = static_cast<uint32_t>(lst.m_vec.size() - m_shieldedList.size() + (shieldedId - startIndex));
            }
            Lelantus::Prover prover(lst, pKrn->m_SpendProof);
            {
                auto shieldedCoin = m_Tx.GetWalletDB()->getShieldedCoin(shieldedId);
                if (!shieldedCoin)
                {
                    throw TransactionFailedException(false, TxFailureReason::NoInputs);
                }

                ShieldedTxo::Viewer viewer;
                viewer.FromOwner(*pMaster, shieldedCoin->m_CoinID.m_Key.m_nIdx);

                ShieldedTxo::DataParams sdp;
                Restore(sdp, *shieldedCoin, viewer);

                Key::IKdf::Ptr pSerialPrivate;
                ShieldedTxo::Viewer::GenerateSerPrivate(pSerialPrivate, *pMaster, shieldedCoin->m_CoinID.m_Key.m_nIdx);
                pSerialPrivate->DeriveKey(prover.m_Witness.V.m_SpendSk, sdp.m_Ticket.m_SerialPreimage);

                prover.m_Witness.V.m_L = shieldedWindowId;
                prover.m_Witness.V.m_R = sdp.m_Ticket.m_pK[0];
                prover.m_Witness.V.m_R += sdp.m_Output.m_k;
                prover.m_Witness.V.m_V = IsAssetTx() ? GetAmount() : GetAmount() + GetFee();

                pKrn->UpdateMsg();
                shieldedCoin->m_CoinID.get_SkOut(prover.m_Witness.V.m_R_Output, pKrn->m_Fee, *pMaster);

                ExecutorMT exec;
                Executor::Scope scope(exec);
                pKrn->Sign(prover, shieldedCoin->m_CoinID.m_AssetID);
            }

            m_Tx.SetParameter(TxParameterID::KernelID, pKrn->m_Internal.m_ID);

            LOG_INFO() << m_Tx.GetTxID() << "[" << m_SubTxID << "]"
                << " Transaction created. Kernel: " << GetKernelIDString()
                << ", min height: " << pKrn->m_Height.m_Min
                << ", max height: " << pKrn->m_Height.m_Max
                << ", shielded id: " << shieldedId
                << ", window start: " << startIndex
                << ", window end: " << pKrn->m_WindowEnd
                << ", window size: " << windowSize
                << ", total shielded outs: " << m_totalShieldedOuts;

            transaction->m_vKernels.push_back(std::move(pKrn));
            ECC::Scalar::Native offset = transaction->m_Offset;
            offset += prover.m_Witness.V.m_R_Output;
            transaction->m_Offset = offset;
        }

        transaction->Normalize();
        return transaction;
    }

    void PullTxBuilder::GenerateCoin(Amount amount, bool isUnlinked)
    {
        Coin newUtxo = m_Tx.GetWalletDB()->generateNewCoin(amount, GetAssetId());

        newUtxo.m_createTxId = m_Tx.GetTxID();
        newUtxo.m_isUnlinked = isUnlinked;
        m_Tx.GetWalletDB()->storeCoin(newUtxo);
        m_OutputCoins.push_back(newUtxo.m_ID);
        m_Tx.SetParameter(TxParameterID::OutputCoins, m_OutputCoins, false, m_SubTxID);
    }

    TxoID PullTxBuilder::GenerateWindowBegin(TxoID shieldedId, uint32_t windowSize)
    {
        if (shieldedId == 0)
            return 0;

        TxoID totalShieldedOuts = 0;
        storage::getVar(*m_Tx.GetWalletDB(), kStateSummaryShieldedOutsDBPath, totalShieldedOuts);

        bool isRestrictedMode = totalShieldedOuts > shieldedId + windowSize + Rules::get().Shielded.MaxWindowBacklog;

        std::random_device rd;
        std::default_random_engine generator(rd());

        if (isRestrictedMode)
        {
            uint32_t restrictedWindowSize = Rules::get().Shielded.m_ProofMin.get_N();
            if (shieldedId < restrictedWindowSize)
            {
                std::uniform_int_distribution<TxoID> distribution(0, shieldedId);
                return distribution(generator);
            }
            else
            {
                std::uniform_int_distribution<TxoID> distribution(shieldedId - restrictedWindowSize + 1, shieldedId);
                return distribution(generator);
            }
        }
        else
        {
            if (shieldedId >= windowSize)
            {
                TxoID maxWindowEnd = std::min(shieldedId + windowSize, totalShieldedOuts);
                TxoID maxWindowBegin = maxWindowEnd - windowSize;
                TxoID minWindowBegin = shieldedId - windowSize + 1;

                if (totalShieldedOuts - windowSize > Rules::get().Shielded.MaxWindowBacklog)
                {                    
                    TxoID normalMinWindowBegin = totalShieldedOuts - Rules::get().Shielded.MaxWindowBacklog - windowSize;
                    minWindowBegin = std::max(minWindowBegin, normalMinWindowBegin);
                }

                assert(minWindowBegin <= shieldedId);

                std::uniform_int_distribution<TxoID> distribution(minWindowBegin, maxWindowBegin);
                return distribution(generator);
            }
            else
            {
                return 0;
            }
        }
    }
} // namespace beam::wallet::lelantus