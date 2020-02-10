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

#include <random>

namespace beam::wallet::lelantus
{
    namespace
    {
        ECC::Scalar::Native RestoreSkSpendKey(Key::IKdf::Ptr pKdf, const ShieldedCoin& shieldedCoin)
        {
            ShieldedTxo::Viewer viewer;
            viewer.FromOwner(*pKdf);

            ShieldedTxo::Data::SerialParams serialParams;
            serialParams.m_pK[0] = shieldedCoin.m_skSerialG;
            serialParams.m_IsCreatedByViewer = shieldedCoin.m_isCreatedByViewer;
            serialParams.Restore(viewer);

            ECC::Scalar::Native skSpendKey;
            Key::IKdf::Ptr pSerialPrivate;
            ShieldedTxo::Viewer::GenerateSerPrivate(pSerialPrivate, *pKdf);
            pSerialPrivate->DeriveKey(skSpendKey, serialParams.m_SerialPreimage);

            return skSpendKey;
        }

        TxoID GenerateWindowBegin(TxoID shieldedId, uint32_t windowSize)
        {
            std::random_device rd;
            std::default_random_engine generator(rd());

            if (shieldedId < windowSize)
            {
                std::uniform_int_distribution<TxoID> distribution(0, shieldedId);
                return distribution(generator);
            }
            else
            {
                auto min = shieldedId - windowSize;
                std::uniform_int_distribution<TxoID> distribution(min + 1, min + windowSize);
                return distribution(generator);
            }
        }
    }

    PullTxBuilder::PullTxBuilder(BaseTransaction& tx, SubTxID subTxID, const AmountList& amount, Amount fee)
        : BaseTxBuilder(tx, subTxID, amount, fee)
    {
    }

    bool PullTxBuilder::GetShieldedList()
    {
        if (m_shieldedList.empty())
        {
            uint32_t windowSize = m_Tx.GetMandatoryParameter<Lelantus::Cfg>(TxParameterID::ShieldedInputCfg).get_N();
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
        TxoID shieldedId = m_Tx.GetMandatoryParameter<TxoID>(TxParameterID::ShieldedOutputId);
        TxoID startIndex = m_Tx.GetMandatoryParameter<TxoID>(TxParameterID::WindowBegin);

        // create transaction
        auto transaction = std::make_shared<Transaction>();

        transaction->m_vOutputs = move(m_Outputs);

        {
            ECC::Scalar::Native offset = Zero;

            for (const auto& id : m_OutputCoins)
            {
                ECC::Scalar::Native outputSk;
                CoinID::Worker(id).Create(outputSk, *id.get_ChildKdf(m_Tx.get_MasterKdfStrict()));
                offset -= outputSk;
            }

            transaction->m_Offset = offset;
        }

        {
            Lelantus::Cfg cfg = m_Tx.GetMandatoryParameter<Lelantus::Cfg>(TxParameterID::ShieldedInputCfg);
            uint32_t windowSize = cfg.get_N();
            TxoID windowEnd = startIndex + m_shieldedList.size();
            bool isRestrictedMode = m_totalShieldedOuts > windowEnd + Rules::get().Shielded.MaxWindowBacklog;
            TxoID restrictedOffset = 0;

            if (isRestrictedMode)
            {
                // TODO: find better solution
                // load Cfg for restricted mode
                cfg = m_Tx.GetMandatoryParameter<Lelantus::Cfg>(TxParameterID::ShieldedInputMinCfg);

                // update parameters
                windowSize = cfg.get_N();
                if ((shieldedId - startIndex) >= windowSize)
                {
                    TxoID maxStartIndex = windowEnd - windowSize;
                    TxoID restrictedStartIndex = std::min(GenerateWindowBegin(shieldedId, windowSize), maxStartIndex);

                    restrictedOffset = restrictedStartIndex - startIndex;
                    startIndex = restrictedStartIndex;
                }
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
                std::copy_n(m_shieldedList.begin() + restrictedOffset, windowSize, lst.m_vec.begin());
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

            ECC::Scalar::Native inputSk = Zero;
            // TODO: need to generate "protected" random and get blinding factor through IPrivateKeyKeeper
            inputSk.GenRandomNnz();

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

                ECC::Scalar::Native skSpendKey = RestoreSkSpendKey(m_Tx.GetWalletDB()->get_MasterKdf(), *shieldedCoin);
                ECC::Scalar::Native witnessSk = shieldedCoin->m_skSerialG;
                witnessSk += shieldedCoin->m_skOutputG;

                prover.m_Witness.V.m_L = shieldedWindowId;
                prover.m_Witness.V.m_R = witnessSk;
                prover.m_Witness.V.m_R_Output = inputSk;
                prover.m_Witness.V.m_SpendSk = skSpendKey;
                prover.m_Witness.V.m_V = GetAmount() + GetFee();

                // update "m_spentTxId" for shieldedCoin
                shieldedCoin->m_spentTxId = m_Tx.GetTxID();
                m_Tx.GetWalletDB()->saveShieldedCoin(*shieldedCoin);
            }
            pKrn->UpdateMsg();

            ECC::Oracle oracle;
            oracle << pKrn->m_Msg;
            prover.Generate(Zero, oracle);

            pKrn->MsgToID();
            m_Tx.SetParameter(TxParameterID::KernelID, pKrn->m_Internal.m_ID);

            transaction->m_vKernels.push_back(std::move(pKrn));

            ECC::Scalar::Native offset = transaction->m_Offset;
            offset += inputSk;
            transaction->m_Offset = offset;
        }

        transaction->Normalize();

        return transaction;
    }
} // namespace beam::wallet::lelantus