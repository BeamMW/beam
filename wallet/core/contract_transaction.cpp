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
        : BaseTransaction (TxType::Contract, context)
    {
    }

    bool ContractTransaction::IsInSafety() const
    {
        const auto txState = GetState<State>();
        return txState == State::KernelConfirmation;
    }

    struct ContractTransaction::MyBuilder
        :public BaseTxBuilder
    {
        using BaseTxBuilder::BaseTxBuilder;

        bool m_ChannelsOpened = false;
        bool m_IsCoSigner = false;

        struct Channel
            :public boost::intrusive::list_base_hook<>
            ,public IRawCommGateway::IHandler
        {
            MyBuilder* m_pThis;
            WalletID m_WidMy;
            WalletID m_WidPeer;
            uint32_t m_Stage = 0;
            Transaction m_TxFromPeer;

            Channel()
            {
                m_TxFromPeer.m_Offset = Zero;
            }

            ~Channel()
            {
                m_pThis->m_Tx.GetGateway().Unlisten(m_WidMy);
            }

            bool IsEmptyInOutKrn() const
            {
                return m_TxFromPeer.m_vInputs.empty() && m_TxFromPeer.m_vOutputs.empty() && m_TxFromPeer.m_vKernels.empty();
            }

            bool IsEmptyOffset() const
            {
                return m_TxFromPeer.m_Offset.m_Value == Zero;
            }

            bool ReadSig(ECC::Scalar& out)
            {
                if (!IsEmptyInOutKrn() || IsEmptyOffset())
                    return false;

                out = m_TxFromPeer.m_Offset;
                m_TxFromPeer.m_Offset = Zero;

                return true;
            }

            static void DeriveSharedSk(ECC::Scalar::Native& skOut, const ECC::Scalar::Native& skMy, const ECC::Point::Native& ptForeign)
            {
                ECC::Point::Native pt = ptForeign * skMy;
                ECC::Point pk = pt;

                ECC::Oracle o;
                o << "dh.contract";
                o << pk;
                o >> skOut;
            }

            void OnMsg(const Blob& d) override
            {
                try
                {
                    Deserializer der;
                    der.reset(d.p, d.n);

                    der & m_TxFromPeer;

                    m_pThis->m_Tx.UpdateAsync();
                }
                catch (std::exception&)
                {
                    // ignore
                }
            }

            void SendTx(const Transaction& tx)
            {
                Serializer ser;
                ser& tx;
                auto res = ser.buffer();

                m_pThis->m_Tx.GetGateway().Send(m_WidPeer, Blob(res.first, (uint32_t)res.second));
            }

            void SendSig(const ECC::Scalar& k)
            {
                Transaction tx;
                tx.m_Offset = k;
                SendTx(tx);
            }
        };

        intrusive::list_autoclear<Channel> m_lstChannels;

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

        static void AddScalar(ECC::Scalar& dst, const ECC::Scalar& src)
        {
            ECC::Scalar::Native k(dst);
            k += ECC::Scalar::Native(src);
            dst = k;
        }

        template <typename T>
        static void MoveVectorInto(std::vector<T>& dst, std::vector<T>& src)
        {
            size_t n = dst.size();
            dst.resize(n + src.size());

            for (size_t i = 0; i < src.size(); i++)
                dst[n + i] = std::move(src[i]);

            src.clear();
        }
    };


    void ContractTransaction::UpdateImpl()
    {
        if (!m_TxBuilder)
            m_TxBuilder = std::make_shared<MyBuilder>(*this, kDefaultSubTxID);

        auto& builder = *m_TxBuilder;

        Key::IKdf::Ptr pKdf = get_MasterKdfStrict();

        bvm2::ContractInvokeData vData;
        GetParameter(TxParameterID::ContractDataPacked, vData, GetSubTxID());

        if (!builder.m_ChannelsOpened)
        {
            for (uint32_t i = 0; i < vData.size(); i++)
            {
                const auto& cdata = vData[i];
                for (uint32_t iC = 0; iC < cdata.m_Adv.m_vCosigners.size(); iC++)
                {
                    bool b = cdata.IsCoSigner();
                    if (builder.m_lstChannels.empty())
                        builder.m_IsCoSigner = b;
                    else
                    {
                        if (builder.m_IsCoSigner != b)
                            throw TransactionFailedException(false, TxFailureReason::Unknown);
                    }

                    auto* pC = builder.m_lstChannels.Create_back();
                    pC->m_pThis = &builder;

                    ECC::Scalar::Native sk;
                    pKdf->DeriveKey(sk, cdata.m_Adv.m_hvSk);

                    ECC::Point::Native pt;
                    if (!pt.ImportNnz(cdata.m_Adv.m_vCosigners[iC]))
                        throw TransactionFailedException(false, TxFailureReason::Unknown);

                    ECC::Scalar::Native sk2;
                    pC->DeriveSharedSk(sk2, sk, pt);

                    sk = sk * sk2;
                    pt = pt * sk2;

                    pC->m_WidMy.m_Pk.FromSk(sk);
                    pC->m_WidMy.SetChannelFromPk();

                    pC->m_WidPeer.m_Pk.Import(pt);
                    pC->m_WidPeer.SetChannelFromPk();

                    GetGateway().Listen(pC->m_WidMy, sk, pC);
                }
            }

            builder.m_ChannelsOpened = true;
        }

        const HeightHash* pParentCtx = nullptr;
        for (uint32_t i = 0; i < vData.size(); i++)
        {
            const auto& cdata = vData[i];
            if (bvm2::ContractInvokeEntry::Flags::Dependent & cdata.m_Flags)
            {
                pParentCtx = &cdata.m_ParentCtx;
                break;
            }
        }

        auto s = GetState<State>();
        if (State::Initial == s)
        {
            UpdateTxDescription(TxStatus::InProgress);

            if (pParentCtx)
            {
                builder.m_Height = pParentCtx->m_Height;
                SetParameter(TxParameterID::MinHeight, builder.m_Height.m_Min, GetSubTxID());
                SetParameter(TxParameterID::MaxHeight, builder.m_Height.m_Max, GetSubTxID());
            }
            else
            {
                Block::SystemState::Full sTip;
                if (GetTip(sTip))
                {
                    builder.m_Height.m_Max = sTip.m_Height + 20; // 20 blocks - standard contract tx life time
                    SetParameter(TxParameterID::MaxHeight, builder.m_Height.m_Max, GetSubTxID());
                }
            }

            if (vData.empty())
                throw TransactionFailedException(false, TxFailureReason::Unknown);

            bvm2::FundsMap fm;

            for (uint32_t i = 0; i < vData.size(); i++)
            {
                const auto& cdata = vData[i];

                Amount fee;
                if (cdata.IsAdvanced())
                    fee = cdata.m_Adv.m_Fee; // can't change!
                else
                {
                    fee = cdata.get_FeeMin(builder.m_Height.m_Min);
                    if (!i)
                        std::setmax(fee, builder.m_Fee);
                }

                cdata.Generate(*builder.m_pTransaction, *pKdf, builder.m_Height, fee);

                fm += cdata.m_Spend;

                if (!builder.m_IsCoSigner)
                    fm[0] += fee;
            }

            BaseTxBuilder::Balance bb(builder);
            for (auto it = fm.begin(); fm.end() != it; it++)
                bb.m_Map[it->first].m_Value -= it->second;

            bb.CompleteBalance(); // will select coins as needed
            builder.SaveCoins();

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

            s = builder.m_lstChannels.empty() ?  State::Registration : State::Negotiating;
            SetState(s);
        }

        if (State::Negotiating == s)
        {
            bool bStillNegotiating = false;
            bool bTxDirty = false;
            bool bWaitingK = false;

            auto it = builder.m_lstChannels.begin();

            for (uint32_t i = 0; i < vData.size(); i++)
            {
                const auto& cdata = vData[i];
                if (cdata.m_Adv.m_vCosigners.empty())
                    continue;
                assert(cdata.IsAdvanced());

                for (uint32_t iC = 0; iC < cdata.m_Adv.m_vCosigners.size(); iC++)
                {
                    assert(builder.m_pTransaction && (i < builder.m_pTransaction->m_vKernels.size()));
                    auto& krn0 = *builder.m_pTransaction->m_vKernels[i];
                    auto& krn = Cast::Up<TxKernelContractControl>(krn0);

                    assert(builder.m_lstChannels.end() != it);
                    auto& ch = *it++;

                    if (builder.m_IsCoSigner)
                    {
                        if (!ch.m_Stage)
                        {
                            // send my sig
                            ch.SendSig(krn.m_Signature.m_k);
                            bTxDirty = true;
                            ch.m_Stage = 1;
                        }

                        if ((1 == ch.m_Stage) && ch.ReadSig(krn.m_Signature.m_k))
                        {
                            krn.UpdateID();
                            // TODO test sig

                            bTxDirty = true;
                            ch.m_Stage = 2;
                        }

                        if (ch.m_Stage < 2)
                            bStillNegotiating = true;
                    }
                    else
                    {
                        if (!ch.m_Stage)
                        {
                            ECC::Scalar k;
                            if (ch.ReadSig(k))
                            {
                                bTxDirty = true;
                                ch.m_Stage = 1;

                                MyBuilder::AddScalar(krn.m_Signature.m_k, k);
                            }
                            else
                                bWaitingK = true;
                        }

                        if ((2 == ch.m_Stage) && !(ch.IsEmptyInOutKrn() && ch.IsEmptyOffset()))
                        {
                            // add to our tx
                            MyBuilder::MoveVectorInto(builder.m_pTransaction->m_vInputs, ch.m_TxFromPeer.m_vInputs);
                            MyBuilder::MoveVectorInto(builder.m_pTransaction->m_vOutputs, ch.m_TxFromPeer.m_vOutputs);
                            MyBuilder::MoveVectorInto(builder.m_pTransaction->m_vKernels, ch.m_TxFromPeer.m_vKernels);
                            MyBuilder::AddScalar(builder.m_pTransaction->m_Offset, ch.m_TxFromPeer.m_Offset);
                            ch.m_TxFromPeer.m_Offset.m_Value = Zero;

                            ch.m_Stage = 3;
                            bTxDirty = true;
                        }

                        if (ch.m_Stage < 3)
                            bStillNegotiating = true;

                    }
                }
            }

            if (builder.m_IsCoSigner)
            {
                if (!bStillNegotiating)
                {
                    // time to send my tx part. Exclude the multisig kernels
                    std::vector<TxKernel::Ptr> v1, v2;

                    for (uint32_t i = 0; i < vData.size(); i++)
                    {
                        const auto& cdata = vData[i];
                        bool bMultisig = !cdata.m_Adv.m_vCosigners.empty();
                        (bMultisig ? v1 : v2).push_back(std::move(builder.m_pTransaction->m_vKernels[i]));
                    }
                    builder.m_pTransaction->m_vKernels = std::move(v2);

                    for (it = builder.m_lstChannels.begin(); builder.m_lstChannels.end() != it; )
                    {
                        auto& ch = *it++;
                        ch.SendTx(*builder.m_pTransaction);
                    }

                    builder.m_pTransaction->m_vKernels.swap(v2);

                    uint32_t n1 = 0, n2 = 0;

                    for (uint32_t i = 0; i < vData.size(); i++)
                    {
                        const auto& cdata = vData[i];
                        bool bMultisig = !cdata.m_Adv.m_vCosigners.empty();

                        auto& pKrn = bMultisig ? v1[n1++] : v2[n2++];
                        builder.m_pTransaction->m_vKernels.push_back(std::move(pKrn));
                    }
                }
            }
            else
            {
                it = builder.m_lstChannels.begin();

                if (bStillNegotiating && !bWaitingK)
                {
                    for (uint32_t i = 0; i < vData.size(); i++)
                    {
                        const auto& cdata = vData[i];
                        for (uint32_t iC = 0; iC < cdata.m_Adv.m_vCosigners.size(); iC++)
                        {
                            auto& krn0 = *builder.m_pTransaction->m_vKernels[i];
                            auto& krn = Cast::Up<TxKernelContractControl>(krn0);
                            auto& ch = *it++;

                            if (1 == ch.m_Stage)
                            {
                                ch.m_Stage = 2;
                                bTxDirty = true;

                                krn.UpdateID();
                                // TODO test sig

                                ch.SendSig(krn.m_Signature.m_k);
                            }
                        }
                    }


                }
            }

            if (bTxDirty)
                builder.OnSigned();

            if (bStillNegotiating)
                return;

            if (!builder.m_IsCoSigner)
                builder.FinalyzeTx();

            s = Registration;
            SetState(s);
        }

        if (!builder.m_IsCoSigner)
        {
            // We're the tx owner
            uint8_t nRegistered = proto::TxStatus::Unspecified;
            if (!GetParameter(TxParameterID::TransactionRegistered, nRegistered))
            {
                if (CheckExpired())
                    return;

                GetGateway().register_tx(GetTxID(), builder.m_pTransaction, pParentCtx ? &pParentCtx->m_Hash : nullptr);
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
}
