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

#include "contract_transaction.h"

#include "base_tx_builder.h"
#include "wallet.h"
#include "bvm/ManagerStd.h"

namespace beam::wallet
{
    ContractTransaction::Creator::Creator(IWalletDB::Ptr walletDB)
        : m_WalletDB(walletDB)
    {
    }

    BaseTransaction::Ptr ContractTransaction::Creator::Create(const TxContext& context)
    {
        return BaseTransaction::Ptr(new ContractTransaction(context));
    }

    ContractTransaction::ContractTransaction(const TxContext& context)
        : BaseTransaction{ context }
    {
    }

    TxType ContractTransaction::GetType() const
    {
        return TxType::Contract;
    }

    bool ContractTransaction::IsInSafety() const
    {
        State txState = GetState();
        return txState == State::KernelConfirmation;
    }

    struct ContractTransaction::MyBuilder
        :public BaseTxBuilder
    {
        using BaseTxBuilder::BaseTxBuilder;

        void AddCoinOffsets(const Key::IKdf::Ptr& pKdf)
        {
            ECC::Scalar::Native kOffs;
            m_Coins.AddOffset(kOffs, pKdf);
            AddOffset(kOffs);
        }

        void OnSigned()
        {
            m_pKrn = m_pTransaction->m_vKernels.front().get(); // if there're many - let it be the 1st contract kernel
            SaveKernel();
            SaveKernelID();
            SaveInOuts();
        }
    };


    void ContractTransaction::UpdateImpl()
    {
        if (!m_TxBuilder)
            m_TxBuilder = std::make_shared<MyBuilder>(*this, kDefaultSubTxID);
        auto& builder = *m_TxBuilder;

        Key::IKdf::Ptr pKdf = get_MasterKdfStrict();

        State s = GetState();
        if (State::Initial == s)
        {
            UpdateTxDescription(TxStatus::InProgress);

            Block::SystemState::Full sTip;
            if (GetTip(sTip))
            {
                builder.m_Height.m_Max = sTip.m_Height + kDefaultTxLifetime;
                SetParameter(TxParameterID::MaxHeight, builder.m_Height.m_Max, GetSubTxID());
            }

            std::vector<bvm2::ContractInvokeData> vData;
            GetParameter(TxParameterID::ContractDataPacked, vData, GetSubTxID());

            if (vData.empty())
                throw TransactionFailedException(false, TxFailureReason::Unknown);

            BaseTxBuilder::Balance bb(builder);
            bvm2::FundsMap fm;
            fm += vData;

            for (auto it = fm.begin(); fm.end() != it; it++)
                bb.m_Map[it->first].m_Value -= it->second;

            bb.CompleteBalance(); // will select coins as needed
            builder.SaveCoins();

            for (uint32_t i = 0; i < vData.size(); i++)
            {
                const auto& cdata = vData[i];
                cdata.Generate(*builder.m_pTransaction, *pKdf, builder.m_Height);
            }

            builder.AddCoinOffsets(pKdf);
            builder.OnSigned();

            s = State::GeneratingCoins;
            SetState(s);
        }

        if (State::GeneratingCoins == s)
        {
            builder.GenerateInOuts();
            if (builder.IsGeneratingInOuts())
                return;

            builder.FinalyzeTx();

            s = Registration;
            SetState(s);
        }

        {
            // We're the tx owner
            uint8_t nRegistered = proto::TxStatus::Unspecified;
            if (!GetParameter(TxParameterID::TransactionRegistered, nRegistered))
            {
                if (CheckExpired())
                    return;

                GetGateway().register_tx(GetTxID(), builder.m_pTransaction);
                SetState(State::Registration);
                return;
            }

            if (proto::TxStatus::Ok != nRegistered) // we have to ensure that this transaction hasn't already added to blockchain)
            {
                Height lastUnconfirmedHeight = 0;
                if (GetParameter(TxParameterID::KernelUnconfirmedHeight, lastUnconfirmedHeight) && lastUnconfirmedHeight > 0)
                {
                    OnFailed(TxFailureReason::FailedToRegister, true);
                    return;
                }
            }
        }

        Height hProof = 0;
        GetParameter(TxParameterID::KernelProofHeight, hProof);
        if (!hProof)
        {
            SetState(State::KernelConfirmation);
            ConfirmKernel(builder.m_pKrn->m_Internal.m_ID);
            return;
        }

        SetCompletedTxCoinStatuses(hProof);
        CompleteTx();
    }

    ContractTransaction::State ContractTransaction::GetState() const
    {
        State state = State::Initial;
        GetParameter(TxParameterID::State, state);
        return state;
    }

}
