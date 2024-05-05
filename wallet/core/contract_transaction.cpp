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
#include "contracts/shaders_manager.h"

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
        return txState == State::Registration;
    }

    struct ContractTransaction::MyBuilder
        :public BaseTxBuilder
    {
        using BaseTxBuilder::BaseTxBuilder;

        bvm2::ContractInvokeData m_Data;
        const HeightHash* m_pParentCtx = nullptr;
        uint32_t m_TxMask = 0;
        bool m_HftSubscribed = false;
        bool m_RebuildFailed = false;
        bool m_AllUnconfirmed = false;
        bool m_ExplicitMaxSpend = false;
        boost::optional<Merkle::Hash> m_pNewCtx;

        static void Fail(const char* sz = nullptr)
        {
            throw TransactionFailedException(false, TxFailureReason::Unknown, sz);
        }

        struct HftVariant
            :public intrusive::set_base_hook<HeightHash>
        {
            Transaction m_Tx; // built and validated tx
            Coins m_Coins;

		    template <typename Archive>
            void serialize(Archive& ar)
            {
                ar
                    & m_Key
                    & m_Tx
                    & m_Coins.m_Input
                    & m_Coins.m_InputShielded
                    & m_Coins.m_Output;
            }
        };

        struct HftState
        {
            intrusive::multiset_autoclear<HftVariant> m_Variants;
            bvm2::FundsMap m_SpendInitial;

            intrusive::multiset_autoclear<HftVariant>::iterator FindVariantFrom(Height h)
            {
                HeightHash hh;
                hh.m_Height = h;
                hh.m_Hash = Zero;
                return m_Variants.lower_bound(hh, HftVariant::Comparator());
            }

            void Ser(Serializer& ser) const
            {
                ser & Cast::Down< std::map<Asset::ID, AmountSigned> >(m_SpendInitial);

                size_t n = m_Variants.size();
                ser & n;

                for (const auto& x : m_Variants)
                    ser & x;
            }

            void Der(Deserializer& der)
            {
                der & Cast::Down< std::map<Asset::ID, AmountSigned> >(m_SpendInitial);

                size_t n = 0;
                der & n;

                while (n--)
                {
                    auto pHftv = std::make_unique<HftVariant>();
                    der & *pHftv;
                    m_Variants.insert(*pHftv.release());
                }
            }

        } m_HftState;

        struct KernelProofContext
        {
            typedef std::shared_ptr<KernelProofContext> Ptr;

            MyBuilder* m_pThis;
            uint32_t m_Remaining;
            Height m_hTipAtStart;
        };

        KernelProofContext::Ptr m_pAsyncCtx;

        void DeleteAsyncCtx()
        {
            if (m_pAsyncCtx)
            {
                m_pAsyncCtx->m_pThis = nullptr;
                m_pAsyncCtx.reset();
            }
        }

        void OnRebuildFailed();

        bool IsHftPending();

        void ConfirmHfts(Height hLastUnconfirmed, Height hTip);

        void EnsureNewCtx();

        virtual ~MyBuilder()
        {
            DeleteAsyncCtx();

            if (m_HftSubscribed)
                m_Tx.GetGateway().HftSubscribe(false);
        }

        bool CanRebuildHft() const
        {
            return m_HftSubscribed && !m_Data.m_AppInvoke.m_App.empty();
        }

        struct KernelCallback
            :public INegotiatorGateway::IConfirmCallback
        {
            KernelProofContext::Ptr m_pAsyncCtx;
            HftVariant* m_pHftv;
            HeightHash m_Hh;

            void OnDone(const Height* pHeight) override
            {
                auto* pThis = m_pAsyncCtx->m_pThis;
                if (pThis)
                    pThis->OnKernelConfirmed(m_pHftv, m_Hh, pHeight);
            }
        };

        void ConfirmKernelEx(HftVariant*);
        void OnKernelConfirmed(HftVariant*, const HeightHash&, const Height*);
        void SaveToHftv();

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

        struct AppShaderExec :public wallet::ManagerStdInWallet
        {
            typedef std::unique_ptr<AppShaderExec> Ptr;

            MyBuilder* m_pBuilder; // set to null when finished
            bool m_Err = false;

            using ManagerStdInWallet::ManagerStdInWallet;

            void SwapParams()
            {
                m_BodyManager.swap(m_pBuilder->m_Data.m_AppInvoke.m_App);
                m_BodyContract.swap(m_pBuilder->m_Data.m_AppInvoke.m_Contract);
                m_Args.swap(m_pBuilder->m_Data.m_AppInvoke.m_Args);
            }

            void OnDone(const std::exception* pExc) override
            {
                assert(m_pBuilder);

                m_Err = !!pExc;
                if (pExc)
                    std::cout << "Shader exec error: " << pExc->what() << std::endl;
                else
                    std::cout << "Shader output: " << m_Out.str() << std::endl;

                m_pBuilder->m_Tx.UpdateAsync();
                m_pBuilder = nullptr;
            }

            void SetExplicitContext(Height h, const Merkle::Hash& hv)
            {
                m_Context.m_Height = h;
                m_Context.m_pParent = std::make_unique<Merkle::Hash>(hv);
            }

            void OnReset() override
            {
                ManagerStdInWallet::OnReset();

                if (m_pBuilder)
                    m_fmSpendMax = m_pBuilder->m_Data.m_SpendMax;
            }
        };

        AppShaderExec::Ptr m_pAppExec;

        void SetParentCtx()
        {
            m_pParentCtx = nullptr;

            for (const auto& cdata : m_Data.m_vec)
            {
                if (bvm2::ContractInvokeEntry::Flags::Dependent & cdata.m_Flags)
                {
                    m_pParentCtx = &cdata.m_ParentCtx;
                    break;
                }
            }
        }

        struct FundsCmpWalker
        {
            struct PerMap
            {
                bvm2::FundsMap::const_iterator m_it, m_itEnd;
                AmountSigned m_Val;

                void Init(const bvm2::FundsMap& fm)
                {
                    m_it = fm.begin();
                    m_itEnd = fm.end();
                }

                void Move()
                {
                    m_Val = m_it->second;
                    m_it++;
                }
            };

            PerMap m_pm0, m_pm1;

            FundsCmpWalker(const bvm2::FundsMap& fm0, const bvm2::FundsMap& fm1)
            {
                m_pm0.Init(fm0);
                m_pm1.Init(fm1);
            }

            bool MoveNext()
            {
                m_pm0.m_Val = 0;
                m_pm1.m_Val = 0;

                if (m_pm0.m_it == m_pm0.m_itEnd)
                {
                    if (m_pm1.m_it == m_pm1.m_itEnd)
                        return false;

                    m_pm1.Move();
                }
                else
                {
                    if (m_pm1.m_it == m_pm1.m_itEnd)
                        m_pm0.Move();
                    else
                    {
                        bool b0 = (m_pm0.m_it->first <= m_pm1.m_it->first);
                        bool b1 = (m_pm1.m_it->first <= m_pm0.m_it->first);

                        if (b0)
                            m_pm0.Move();
                        if (b1)
                            m_pm1.Move();
                    }
                }

                return true;
            }
        };

        bool IsSpendWithinLimitsUns(Amount v0, Amount v1) const
        {
            if (v1 <= v0)
                return true;

            if (m_ExplicitMaxSpend)
                return false;

            Amount dv = v1 - v0;
            return dv <= v0 / 100; // assume implicit threshold is 1%
        }

        bool IsSpendWithinLimits(const bvm2::FundsMap& fm) const
        {
            for (FundsCmpWalker fcw(m_ExplicitMaxSpend ? m_Data.m_SpendMax : m_HftState.m_SpendInitial, fm); fcw.MoveNext(); )
            {
                if (fcw.m_pm1.m_Val > 0)
                {
                    // spending
                    if (fcw.m_pm0.m_Val <= 0)
                        return false;

                    if (!IsSpendWithinLimitsUns(fcw.m_pm0.m_Val, fcw.m_pm1.m_Val))
                        return false;
                }
                else
                {
                    // receiving
                    if (fcw.m_pm0.m_Val >= 0)
                        continue;

                    if (!IsSpendWithinLimitsUns(0-fcw.m_pm1.m_Val, 0-fcw.m_pm0.m_Val))
                        return false;
                }
            }

            return true;
        }
    };

    void ContractTransaction::Init()
    {
        assert(!m_TxBuilder);

        m_TxBuilder = std::make_shared<MyBuilder>(*this, kDefaultSubTxID);
        auto& builder = *m_TxBuilder;

        GetParameter(TxParameterID::ContractDataPacked, builder.m_Data, GetSubTxID());

        if (!builder.m_Data.m_vec.empty() && (bvm2::ContractInvokeEntry::Flags::SaveSpendMax & builder.m_Data.m_vec.front().m_Flags))
            builder.m_ExplicitMaxSpend = true;

        {
            ByteBuffer buf;
            GetParameter(TxParameterID::HftState, buf);
            if (!buf.empty())
            {
                Deserializer der;
                der.reset(&buf.front(), buf.size());
                builder.m_HftState.Der(der);
            }
        }


        builder.SetParentCtx();

        if (builder.m_pParentCtx)
        {
            builder.m_HftSubscribed = true;
            GetGateway().HftSubscribe(true);
        }
    }

    void ContractTransaction::OnDependentStateChanged()
    {
        UpdateAsync();
    }

    bool ContractTransaction::MyBuilder::IsHftPending()
    {
        Block::SystemState::Full sTip;
        if (!m_Tx.GetTip(sTip))
            return false;

        uint32_t nCount = 0;
        const auto* pHv = m_Tx.GetGateway().get_DependentState(nCount);
        Height h = sTip.m_Height + 1;

        EnsureNewCtx();

        for (uint32_t i = 0; i < nCount; i++)
        {
            if (m_pNewCtx && (m_pParentCtx->m_Height == h) && (pHv[i] == *m_pNewCtx))
                return true;

            HeightHash hh;
            hh.m_Height = h;
            hh.m_Hash = pHv[i];
            auto it = m_HftState.m_Variants.find(hh, HftVariant::Comparator());
            if (m_HftState.m_Variants.end() != it)
                return true;
        }

        return false;
    }

    void ContractTransaction::MyBuilder::OnRebuildFailed()
    {
        BEAM_LOG_INFO() << "TxoID=" << m_Tx.GetTxID() << " HFT rebuild failed";
        m_RebuildFailed = true;
        m_Tx.SetState(State::Registration);
    }

    bool ContractTransaction::BuildTxOnce()
    {
        auto& builder = *m_TxBuilder;

        Key::IKdf::Ptr pKdf = get_MasterKdfStrict();
        auto& vData = builder.m_Data;
        auto s = GetState<State>();

        Block::SystemState::Full sTip;
        if (!GetTip(sTip))
            return false;

        if (State::RebuildHft == s)
        {
            if (!builder.m_pAppExec)
            {
                if (builder.IsHftPending())
                {
                    builder.OnRebuildFailed();
                    BEAM_LOG_INFO() << "already pending";
                    return true;
                }

                // before rebuilding a tx make sure no prev variant accepted
                Height hUnconfirmed = 0;
                GetParameter(TxParameterID::KernelUnconfirmedHeight, hUnconfirmed);

                auto it = builder.m_HftState.FindVariantFrom(hUnconfirmed + 1);
                if ((builder.m_HftState.m_Variants.end() != it) && (it->m_Key.m_Height <= sTip.m_Height))
                {
                    builder.OnRebuildFailed();
                    BEAM_LOG_INFO() << "waiting prev unconfirm";
                    return true;
                }

                uint32_t nCount = 0;
                const auto* pHv = GetGateway().get_DependentState(nCount);
                const auto& hvCtx = nCount ? pHv[nCount - 1] : sTip.m_Prev;

                // is it any different now?
                if (builder.m_pParentCtx && (builder.m_pParentCtx->m_Hash == hvCtx))
                {
                    BEAM_LOG_INFO() << "same state";
                    return false; // retry when the state changes
                }

                builder.m_pAppExec = std::make_unique<MyBuilder::AppShaderExec>(m_Context.get_Wallet());
                auto& aex = *builder.m_pAppExec;
                aex.m_pBuilder = &builder;
                aex.SwapParams();
                aex.set_Privilege(vData.m_AppInvoke.m_Privilege);


                aex.SetExplicitContext(sTip.m_Height, hvCtx);

                aex.m_EnforceDependent = true;

                builder.m_pAppExec->StartRun(1);
            }

            auto& aex = *builder.m_pAppExec;
            if (aex.m_pBuilder)
                return false; // still running

            MyBuilder::AppShaderExec::Ptr pGuard = std::move(builder.m_pAppExec);
            if (aex.m_Err)
            {
                builder.OnRebuildFailed();
                return true;
            }

            if (aex.m_InvokeData.m_vec.empty())
            {
                builder.OnRebuildFailed();
                BEAM_LOG_INFO() << "no invoke";
                return true;
            }

            Cast::Down<bvm2::ContractInvokeDataBase>(vData) = std::move(aex.m_InvokeData);

            aex.m_pBuilder = &builder;
            aex.SwapParams();

            builder.m_pAppExec.reset();

            builder.SetParentCtx();
            if (!builder.m_pParentCtx)
            {
                builder.OnRebuildFailed();
                BEAM_LOG_INFO() << "not HFT";
                return true;
            }

            s = State::Initial;
            SetState(s);
        }

        if (State::Initial == s)
        {
            UpdateTxDescription(TxStatus::InProgress);

            if (builder.m_pParentCtx)
                builder.m_Height = builder.m_pParentCtx->m_Height;
            else
            {
                builder.m_Height.m_Min = sTip.m_Height + 1;
                builder.m_Height.m_Max = sTip.m_Height + 5; // 5 blocks - standard contract tx life time
            }

            if (vData.m_vec.empty())
                builder.Fail();

            bvm2::FundsMap fm = vData.get_FullSpend();

            bool bCheckLimits = builder.m_ExplicitMaxSpend || !builder.m_HftState.m_Variants.empty();
            if (bCheckLimits && !builder.IsSpendWithinLimits(fm))
            {
                builder.OnRebuildFailed();
                BEAM_LOG_INFO() << "slippage too large";

                if (builder.m_HftState.m_Variants.empty())
                {
                    // violated on the initial args
                    OnFailed(TxFailureReason::TransactionExpired);
                    return false;
                }

                return true;
            }

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
                return false;

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
                return false;

            builder.OnSigned();

            if (vData.m_IsSender)
            {
                builder.TestSigs();
                builder.FinalyzeTx();
            }

            s = Registration;
            SetState(s);
        }

        return (State::Registration == s);
    }

    void ContractTransaction::UpdateImpl()
    {
        if (!m_TxBuilder)
            Init();

        auto& builder = *m_TxBuilder;
        bool bVal = true;

        if (builder.m_HftState.m_Variants.empty())
            bVal = BuildTxOnce();
        else
        {
            try
            {
                bVal = BuildTxOnce();
            }
            catch (const std::exception& e)
            {
                builder.OnRebuildFailed();
                BEAM_LOG_INFO() << "TxoID=" << GetTxID() << " error " << e.what();
            }
        }

        if (!bVal)
            return;

        int ret = RegisterTx();
        if (ret < 0)
        {
            // expired. Check if it's an HFT tx that can be rebuilt
            if (builder.m_pAsyncCtx && builder.m_pAsyncCtx->m_Remaining)
                return;

            if (RetryHft())
            {
                BEAM_LOG_INFO() << "TxoID=" << GetTxID() << " Expired. Retrying HFT tx";
                UpdateAsync();
            }
            else
            {
                if (builder.m_AllUnconfirmed)
                    OnFailed(TxFailureReason::TransactionExpired);
                else
                    UpdateOnNextTip();
            }
        }
    }

    void ContractTransaction::MyBuilder::ConfirmKernelEx(HftVariant* pV)
    {
        auto pCallback = std::make_unique<KernelCallback>();
        pCallback->m_pHftv = pV;
        if (!pV)
        {
            EnsureNewCtx();
            pCallback->m_Hh.m_Height = m_pParentCtx->m_Height;
            pCallback->m_Hh.m_Hash = *m_pNewCtx;
        }
        
        assert(m_pAsyncCtx);
        pCallback->m_pAsyncCtx = m_pAsyncCtx;
        m_pAsyncCtx->m_Remaining++;

        const auto& tx = pV ? pV->m_Tx : *m_pTransaction;
        m_Tx.GetGateway().confirm_kernel_ex(tx.m_vKernels.front()->m_Internal.m_ID, std::move(pCallback));
    }

    void ContractTransaction::MyBuilder::OnKernelConfirmed(HftVariant* pHf, const HeightHash& hh, const Height* pHeight)
    {
        assert(m_pAsyncCtx && m_pAsyncCtx->m_Remaining);
        m_pAsyncCtx->m_Remaining--;

        if (pHeight)
        {
            if (!pHf)
            {
                // check if it's the current tx
                EnsureNewCtx();
                if (!m_pNewCtx || (*m_pNewCtx != hh.m_Hash))
                {
                    auto it = m_HftState.m_Variants.find(hh, HftVariant::Comparator());
                    assert(m_HftState.m_Variants.end() != it);
                    pHf = &(*it);
                }
            }

            BEAM_LOG_INFO() << "TxoID=" << m_Tx.GetTxID() << " HFT confirmed";

            SetParameter(TxParameterID::KernelProofHeight, *pHeight);
            DeleteAsyncCtx();

            if (pHf)
            {
                // substitute that tx params
                SetParameter(TxParameterID::InputCoins, pHf->m_Coins.m_Input);
                SetParameter(TxParameterID::InputCoinsShielded, pHf->m_Coins.m_InputShielded);
                SetParameter(TxParameterID::OutputCoins, pHf->m_Coins.m_Output);

                {
                    TemporarySwap ts(pHf->m_Tx, *m_pTransaction);
                    
                    assert(!m_pTransaction->m_vKernels.empty());
                    TxKernel* pKrn = m_pTransaction->m_vKernels.front().get();
                    TemporarySwap ts2(m_pKrn, pKrn);

                    SaveInOuts();
                    SaveKernel();
                    SaveKernelID();
                }

                SetParameter(TxParameterID::Offset, pHf->m_Tx.m_Offset);

                // mark inputs/outputs appropriately
                {
                    auto pDB = m_Tx.GetWalletDB();
                    for (const auto& cid : pHf->m_Coins.m_Input)
                    {
                        Coin coin;
                        coin.m_ID = cid;
                        if (pDB->findCoin(coin))
                        {
                            coin.m_spentTxId = m_Tx.GetTxID();
                            coin.m_spentHeight = *pHeight;
                            pDB->saveCoin(coin);
                        }
                    }

                    for (auto& cid : pHf->m_Coins.m_InputShielded)
                    {
                        auto pCoin = pDB->getShieldedCoin(cid.m_Key);
                        if (pCoin)
                        {
                            pCoin->m_spentTxId = m_Tx.GetTxID();
                            pCoin->m_spentHeight = *pHeight;
                            pDB->saveShieldedCoin(*pCoin);
                        }
                    }

                    for (auto& cid : pHf->m_Coins.m_Output)
                    {
                        Coin c;
                        c.m_ID = cid;
                        c.m_confirmHeight = *pHeight;
                        c.m_createTxId = m_Tx.GetTxID();
                        pDB->saveCoin(c);
                    }
                }

            }
        }
        else
        {
            if (m_pAsyncCtx->m_Remaining)
                return;

            SetParameter(TxParameterID::KernelUnconfirmedHeight, m_pAsyncCtx->m_hTipAtStart);
            BEAM_LOG_INFO() << "TxoID=" << m_Tx.GetTxID() << " HFT unconfirmed at " << m_pAsyncCtx->m_hTipAtStart;
        }

        m_Tx.UpdateAsync();
    }

    bool ContractTransaction::Rollback(Height h)
    {
        if (m_TxBuilder)
            m_TxBuilder->DeleteAsyncCtx();

        return BaseTransaction::Rollback(h);
    }

    void ContractTransaction::MyBuilder::EnsureNewCtx()
    {
        if (m_pNewCtx || m_RebuildFailed || !m_pParentCtx)
            return;

        if (!m_pTransaction || m_pTransaction->m_vKernels.empty())
            return;

        m_pNewCtx.emplace();
        auto& hv = m_pNewCtx.get();

        hv = m_pParentCtx->m_Hash;

        for (const auto& pKrn : m_pTransaction->m_vKernels)
            DependentContext::get_Ancestor(hv, hv, pKrn->m_Internal.m_ID);

        BEAM_LOG_INFO() << "TxoID=" << m_Tx.GetTxID() << " HFT variant: " << *m_pNewCtx;
    }

    void ContractTransaction::MyBuilder::ConfirmHfts(Height hLastUnconfirmed, Height hTip)
    {
        if (!m_HftSubscribed)
            return;

        if (m_pAsyncCtx)
        {
            if (m_pAsyncCtx->m_Remaining)
                return; // pending
        }
        else
        {
            m_pAsyncCtx = std::make_shared<KernelProofContext>();
            m_pAsyncCtx->m_pThis = this;
            m_pAsyncCtx->m_Remaining = 0;
        }

        m_pAsyncCtx->m_hTipAtStart = hTip;

        if (!m_RebuildFailed)
        {
            assert(m_pParentCtx);
            if ((m_pParentCtx->m_Height <= hTip) && (m_pParentCtx->m_Height > hLastUnconfirmed))
                ConfirmKernelEx(nullptr);
        }

        for (auto it = m_HftState.FindVariantFrom(hLastUnconfirmed + 1); m_HftState.m_Variants.end() != it; it++)
        {
            if (it->m_Key.m_Height > hTip)
                break;
            ConfirmKernelEx(&(*it));
        }

        if (m_pAsyncCtx->m_Remaining)
            BEAM_LOG_INFO() << "TxoID=" << m_Tx.GetTxID() << " HFT confirming variants: " << m_pAsyncCtx->m_Remaining;
    }

    int ContractTransaction::RegisterTx()
    {
        auto& builder = *m_TxBuilder;

        // check if anything confirmed
        Height h = 0;
        GetParameter(TxParameterID::KernelProofHeight, h);
        if (h)
        {
            SetCompletedTxCoinStatuses(h);
            CompleteTx();
            return 1;
        }

        Block::SystemState::Full sTip;
        if (!GetTip(sTip))
            sTip.m_Height = 0;

        // check if all unconfirmed
        GetParameter(TxParameterID::KernelUnconfirmedHeight, h);

        builder.m_AllUnconfirmed = builder.m_RebuildFailed || IsExpired(h + 1);
        if (builder.m_AllUnconfirmed)
        {
            if (builder.m_HftState.m_Variants.empty())
                return -1;

            if (h >= builder.m_HftState.m_Variants.rbegin()->m_Key.m_Height)
                return -1;

            builder.m_AllUnconfirmed = false;
        }

        // check if can confirm anything
        builder.ConfirmHfts(h, sTip.m_Height);

        auto itUnconfirmed = builder.m_HftState.FindVariantFrom(h + 1);
        if ((builder.m_HftState.m_Variants.end() != itUnconfirmed) && (itUnconfirmed->m_Key.m_Height <= sTip.m_Height))
            return 0; // confirm or unconfirm prev txs before continuing with this one

        bool bWaitingNewTxRes = false;
        // check if need to send newly-built tx
        if (!builder.m_RebuildFailed && builder.m_Data.m_IsSender)
        {
            builder.EnsureNewCtx();

            uint8_t nTxRegStatus = proto::TxStatus::Unspecified;
            if (!GetParameter(TxParameterID::TransactionRegistered, nTxRegStatus))
            {
                if (!IsExpired(sTip.m_Height + 1))
                {
                    GetGateway().register_tx(GetTxID(), builder.m_pTransaction, builder.m_pParentCtx ? &builder.m_pParentCtx->m_Hash : nullptr);
                    bWaitingNewTxRes = true;
                }
            }
            else
            {
                // assume the status could be due to redundant tx send. Ensure tx hasn't been accepted yet
                if (proto::TxStatus::Ok != nTxRegStatus)
                {
                    switch (nTxRegStatus)
                    {
                    case proto::TxStatus::DependentNoParent:
                    case proto::TxStatus::DependentNotBest:
                    case proto::TxStatus::DependentNoNewCtx:
                        return -1;

                    default:
                        if (!builder.m_HftSubscribed)
                        {
                            OnFailed(TxFailureReason::FailedToRegister, true);
                            return 0;
                        }
                    }
                }
            }
        }

        if (builder.m_HftSubscribed)
        {
            if (!(bWaitingNewTxRes || builder.IsHftPending()))
                return -1;
            UpdateOnNextTip();
        }
        else
            ConfirmKernel(builder.m_pKrn->m_Internal.m_ID);

        return 0;
    }

    bool ContractTransaction::IsExpired(Height hTrg)
    {
        auto& builder = *m_TxBuilder;
        assert(!builder.m_RebuildFailed);

        Height hMax;
        if (builder.m_pParentCtx)
            hMax = builder.m_pParentCtx->m_Height;
        else
        {
            if (builder.m_pKrn)
                hMax = builder.m_pKrn->get_EffectiveHeightRange().m_Max;
            else
            {
                if (!GetParameter(TxParameterID::MaxHeight, hMax))
                    return false;
            }
        }

        return (hMax < hTrg);
    }

    bool ContractTransaction::CheckExpired()
    {
        return false; // disable the outer logic, handle expiration internally
    }

    bool ContractTransaction::CanCancel() const
    {
        if (!m_TxBuilder)
            return true;

        if (m_TxBuilder->m_Data.m_IsSender)
            return true;

        return GetState<State>() != State::Registration;
    }

    bool ContractTransaction::RetryHft()
    {
        auto& builder = *m_TxBuilder;
        if (!builder.CanRebuildHft())
            return false;

        Height h = 0;
        GetParameter(TxParameterID::MinHeight, h);
        if (!h)
        {
            h = m_TxBuilder->m_pKrn->m_Height.m_Min;
            SetParameter(TxParameterID::MinHeight, h);
        }

        Block::SystemState::Full sTip;
        if (!GetTip(sTip))
            return false; //?!

        if (sTip.m_Height - h >= 5)
            return false;

        builder.SaveToHftv();

        SetState(State::RebuildHft);
        return true;
    }

    void ContractTransaction::MyBuilder::SaveToHftv()
    {
        // Release coins, Reset everything
        SetParameter(TxParameterID::TransactionRegistered, Zero);

        {
            auto pDB = m_Tx.GetWalletDB();
            for (const auto& cid : m_Coins.m_Input)
            {
                Coin coin;
                coin.m_ID = cid;
                if (pDB->findCoin(coin))
                {
                    coin.m_spentTxId.reset();
                    pDB->saveCoin(coin);
                }
            }

            for (auto& cid : m_Coins.m_InputShielded)
            {
                auto pCoin = pDB->getShieldedCoin(cid.m_Key);
                if (pCoin)
                {
                    pCoin->m_spentTxId.reset();
                    pDB->saveShieldedCoin(*pCoin);
                }
            }

            pDB->deleteCoinsCreatedByTx(m_Tx.GetTxID());
        }

        SetParameter(TxParameterID::InputCoins, Zero);
        SetParameter(TxParameterID::InputCoinsShielded, Zero);
        SetParameter(TxParameterID::OutputCoins, Zero);

        SetParameter(TxParameterID::Inputs, Zero);
        SetParameter(TxParameterID::ExtraKernels, Zero); // shielded inputs probably
        SetParameter(TxParameterID::Outputs, Zero);

        SetParameter(TxParameterID::Offset, Zero);

        SetParameter(TxParameterID::Kernel, Zero);
        SetParameter(TxParameterID::KernelID, Zero);

        SetParameter(TxParameterID::MutualTxState, Zero); // otherwise BaseTxBuilder won't re-normalize the newly-built tx

        auto pHftv = std::make_unique<HftVariant>();
        pHftv->m_Tx = std::move(*m_pTransaction);
        pHftv->m_Coins = std::move(m_Coins);

        if (m_RebuildFailed)
            m_RebuildFailed = false;
        else
        {
            if (m_HftState.m_Variants.empty() && !m_ExplicitMaxSpend)
                m_HftState.m_SpendInitial = m_Data.get_FullSpend();

            EnsureNewCtx();

            assert(m_pParentCtx && m_pNewCtx);
            pHftv->m_Key.m_Height = m_pParentCtx->m_Height;
            pHftv->m_Key.m_Hash = *m_pNewCtx;

            m_HftState.m_Variants.insert(*pHftv.release());

            Serializer ser;
            m_HftState.Ser(ser);

            ByteBuffer bb;
            ser.swap_buf(bb);

            SetParameter(TxParameterID::HftState, bb);
        }

        m_pTransaction->m_Offset = Zero;

        m_Data.m_vec.clear();

        m_GeneratingInOuts = Stage::None;
        m_Signing = Stage::None;
        m_Status = Status::None;

        m_pKrn = nullptr;
        m_pNewCtx.reset();
        m_pAppExec.reset();
    }


}
