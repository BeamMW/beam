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

        bvm2::ContractInvokeData m_Data;
        const HeightHash* m_pParentCtx = nullptr;
        uint32_t m_TxMask = 0;

        static void Fail(const char* sz = nullptr)
        {
            throw TransactionFailedException(false, TxFailureReason::Unknown, sz);
        }

        struct SigState
        {
            TxKernelContractControl* m_pKrn;
            uint32_t m_RcvMask = 0;
            bool m_Sent = false;
        };

        std::vector<SigState> m_vSigs;

        struct Channel
            :public IRawCommGateway::IHandler
        {
            MyBuilder* m_pThis;
            WalletID m_WidMy;
            WalletID m_WidPeer;

            Channel()
            {
            }

            virtual ~Channel()
            {
                m_pThis->m_Tx.GetGateway().Unlisten(m_WidMy, this);
            }

            uint32_t get_Idx() const
            {
                return static_cast<uint32_t>(this - &m_pThis->m_vChannels.front());
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
                    m_pThis->OnMsg(der, get_Idx());

                    m_pThis->m_Tx.UpdateAsync();
                }
                catch (std::exception&)
                {
                    // ignore
                }
            }

            void Send(Serializer& ser)
            {
                auto res = ser.buffer();
                m_pThis->m_Tx.GetGateway().Send(m_WidPeer, Blob(res.first, (uint32_t)res.second));
            }

            void SendSig(const ECC::Scalar& k, uint32_t iSig)
            {
                Serializer ser;
                ser
                    & iSig
                    & k;

                Send(ser);
            }
        };

        std::vector<Channel> m_vChannels;

        void OnMsg(Deserializer& der, uint32_t iCh)
        {
            uint32_t msk = 1u << iCh;
            uint32_t iSig = 0;
            der & iSig;

            if (iSig < m_vSigs.size())
            {
                auto& st = m_vSigs[iSig];
                if (st.m_RcvMask & msk)
                    Fail();

                ECC::Scalar val;
                der & val;

                if (m_Data.m_IsSender)
                    // partial sig
                    AddScalar(st.m_pKrn->m_Signature.m_k, val);
                else
                    st.m_pKrn->m_Signature.m_k = val; // final sig

                st.m_RcvMask |= msk;
            }
            else
            {
                if (!m_Data.m_IsSender)
                    Fail();

                // tx part
                if (m_TxMask & msk)
                    Fail();
                m_TxMask |= msk;

                Transaction tx;
                der & tx;

                MoveVectorInto(m_pTransaction->m_vInputs, tx.m_vInputs);
                MoveVectorInto(m_pTransaction->m_vOutputs, tx.m_vOutputs);
                MoveVectorInto(m_pTransaction->m_vKernels, tx.m_vKernels);
                AddScalar(m_pTransaction->m_Offset, tx.m_Offset);
            }
        }


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

        void TestSigs()
        {
            for (uint32_t i = 0; i < m_Data.m_vec.size(); i++)
            {
                const auto& cdata = m_Data.m_vec[i];
                if (!cdata.IsMultisigned())
                    continue;

                auto& krn = Cast::Up<TxKernelContractControl>(*m_pTransaction->m_vKernels[i]);

                ECC::Point::Native pt1 = ECC::Context::get().G * ECC::Scalar::Native(krn.m_Signature.m_k);
                ECC::Point::Native pt2;
                pt2.Import(cdata.m_Adv.m_SigImage);

                pt2 += pt1;
                if (pt2 != Zero)
                    Fail("incorrect multisig");

                if (!m_vSigs.empty())
                    krn.UpdateID(); // signature was modified due to the negotiation
            }
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
        Key::IKdf::Ptr pKdf = get_MasterKdfStrict();

        if (!m_TxBuilder)
        {
            m_TxBuilder = std::make_shared<MyBuilder>(*this, kDefaultSubTxID);
            auto& builder = *m_TxBuilder;

            GetParameter(TxParameterID::ContractDataPacked, builder.m_Data, GetSubTxID());

            for (const auto& cdata : builder.m_Data.m_vec)
            {
                if (bvm2::ContractInvokeEntry::Flags::Dependent & cdata.m_Flags)
                {
                    builder.m_pParentCtx = &cdata.m_ParentCtx;
                    break;
                }
            }
        }

        auto& builder = *m_TxBuilder;
        auto& vData = builder.m_Data;


        auto s = GetState<State>();
        if (State::Initial == s)
        {
            UpdateTxDescription(TxStatus::InProgress);

            if (builder.m_pParentCtx)
            {
                builder.m_Height = builder.m_pParentCtx->m_Height;
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

            if (vData.m_vec.empty())
                builder.Fail();

            bvm2::FundsMap fm = vData.get_FullSpend();

            for (uint32_t i = 0; i < vData.m_vec.size(); i++)
            {
                const auto& cdata = vData.m_vec[i];

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

                if (vData.m_IsSender)
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

        if (builder.m_vSigs.empty() && vData.HasMultiSig())
        {
            if (vData.m_vPeers.empty())
                builder.Fail("no peers");

            builder.m_vChannels.reserve(vData.m_vPeers.size());

            ECC::Scalar::Native skMy;
            pKdf->DeriveKey(skMy, vData.m_hvKey);

            for (const auto& pk : vData.m_vPeers)
            {
                auto& c = builder.m_vChannels.emplace_back();
                c.m_pThis = &builder;

                ECC::Point::Native pt;
                if (!pt.ImportNnz(pk))
                    builder.Fail("bad peer");

                ECC::Scalar::Native sk, skMul;
                c.DeriveSharedSk(skMul, skMy, pt);

                sk = skMy * skMul;
                pt = pt * skMul;

                c.m_WidMy.m_Pk.FromSk(sk);
                c.m_WidMy.SetChannelFromPk();

                c.m_WidPeer.m_Pk.Import(pt);
                c.m_WidPeer.SetChannelFromPk();

                GetGateway().Listen(c.m_WidMy, sk, &c);
            }

            for (uint32_t i = 0; i < vData.m_vec.size(); i++)
            {
                const auto& cdata = vData.m_vec[i];
                if (!cdata.IsMultisigned())
                    continue;

                auto& st = builder.m_vSigs.emplace_back();
                st.m_pKrn = &Cast::Up<TxKernelContractControl>(*builder.m_pTransaction->m_vKernels[i]);
            }
        }

        if (State::GeneratingCoins == s)
        {
            builder.GenerateInOuts();
            if (builder.IsGeneratingInOuts())
                return;

            if (builder.m_vSigs.empty())
            {
                builder.TestSigs();
                builder.FinalyzeTx();
                s = State::Registration;
            }
            else
                s = State::Negotiating;

            SetState(s);
        }

        if (State::Negotiating == s)
        {
            bool bStillNegotiating = false;

            uint32_t msk = (1u << builder.m_vChannels.size()) - 1;

            for (uint32_t iSig = 0; iSig < builder.m_vSigs.size(); iSig++)
            {
                auto& st = builder.m_vSigs[iSig];
                bool bSomeMissing = (st.m_RcvMask != msk);
                if (bSomeMissing)
                    bStillNegotiating = true;

                if (!st.m_Sent)
                {
                    if (vData.m_IsSender && bSomeMissing)
                        continue; // not ready yet

                    for (auto& ch : builder.m_vChannels)
                        ch.SendSig(st.m_pKrn->m_Signature.m_k, iSig);

                    st.m_Sent = true;
                }
            }

            if (!bStillNegotiating && (builder.m_TxMask != msk))
            {
                if (vData.m_IsSender)
                    bStillNegotiating = true;
                else
                {
                    builder.TestSigs();

                    Serializer ser;

                    // time to send my tx part. Exclude the multisig kernels
                    {
                        std::vector<TxKernel::Ptr> v1, v2;

                        for (uint32_t i = 0; i < vData.m_vec.size(); i++)
                        {
                            bool bMultisig = vData.m_vec[i].IsMultisigned();
                            (bMultisig ? v1 : v2).push_back(std::move(builder.m_pTransaction->m_vKernels[i]));
                        }
                        builder.m_pTransaction->m_vKernels = std::move(v2);

                        ser
                            & builder.m_vSigs.size()
                            & *builder.m_pTransaction;

                        builder.m_pTransaction->m_vKernels.swap(v2);

                        uint32_t n1 = 0, n2 = 0;
                        for (uint32_t i = 0; i < vData.m_vec.size(); i++)
                        {
                            bool bMultisig = vData.m_vec[i].IsMultisigned();
                            auto& pKrn = bMultisig ? v1[n1++] : v2[n2++];
                            builder.m_pTransaction->m_vKernels.push_back(std::move(pKrn));
                        }
                    }

                    for (auto& ch : builder.m_vChannels)
                        ch.Send(ser);

                    builder.m_TxMask = msk;
                }
            }

            if (bStillNegotiating)
                return;

            builder.OnSigned();

            if (vData.m_IsSender)
            {
                builder.TestSigs();
                builder.FinalyzeTx();
            }

            s = Registration;
            SetState(s);
        }

        if (vData.m_IsSender)
        {
            // We're the tx owner
            uint8_t nRegistered = proto::TxStatus::Unspecified;
            if (!GetParameter(TxParameterID::TransactionRegistered, nRegistered))
            {
                if (CheckExpired())
                    return;

                GetGateway().register_tx(GetTxID(), builder.m_pTransaction, builder.m_pParentCtx ? &builder.m_pParentCtx->m_Hash : nullptr);
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
