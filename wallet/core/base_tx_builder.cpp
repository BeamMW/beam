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

#include "base_tx_builder.h"
#include "core/block_crypt.h"
#include "core/shielded.h"
#include "base_transaction.h"
#include "strings_resources.h"
#include "utility/executor.h"

// TODO: getrandom not available until API 28 in the Android NDK 17b
// https://github.com/boostorg/uuid/issues/76
#if defined(__ANDROID__)
#define BOOST_UUID_RANDOM_PROVIDER_DISABLE_GETRANDOM 1
#endif

#include <boost/uuid/uuid_generators.hpp>
#include <numeric>
#include "utility/logger.h"

namespace beam::wallet
{
    using namespace ECC;
    using namespace std;

    ///////////////////////////////////////
    // BaseTxBuilder::KeyKeeperHandler

    BaseTxBuilder::KeyKeeperHandler::KeyKeeperHandler(BaseTxBuilder& b, Stage& s)
    {
        m_pBuilder = b.weak_from_this();

        m_pStage = &s;
        assert(Stage::None == s);
        s = Stage::InProgress;
    }

    BaseTxBuilder::KeyKeeperHandler::~KeyKeeperHandler()
    {
        if (m_pStage)
        {
            std::shared_ptr<BaseTxBuilder> pBld = m_pBuilder.lock();
            if (pBld)
                Detach(*pBld, Stage::None);
        }
    }

    void BaseTxBuilder::KeyKeeperHandler::Detach(BaseTxBuilder&, Stage s)
    {
        if (m_pStage)
        {
            assert(Stage::InProgress == *m_pStage);
            *m_pStage = s;
            m_pStage = nullptr;
        }
    }

    void BaseTxBuilder::KeyKeeperHandler::OnDone(IPrivateKeyKeeper2::Status::Type n)
    {
        if (m_pStage)
        {
            std::shared_ptr<BaseTxBuilder> pBld = m_pBuilder.lock();
            if (pBld)
            {
                BaseTxBuilder& b = *pBld;
                if (IPrivateKeyKeeper2::Status::Success == n)
                {
                    ITransaction::Ptr pGuard(b.m_Tx.shared_from_this()); // extra ref on transaction object.
                    // Otherwise it can crash in Update() -> CompleteTx(), which will remove its ptr from live tx map

                    try {
                        OnSuccess(b);
                    }
                    catch (const TransactionFailedException& ex) {
                        Detach(b, Stage::None);
                        pBld->m_Tx.OnFailed(ex.GetReason(), ex.ShouldNofify());
                    }
                }
                else
                    OnFailed(b, n);
            }
            else
                m_pStage = nullptr;
        }
    }

    void BaseTxBuilder::KeyKeeperHandler::OnFailed(BaseTxBuilder& b, IPrivateKeyKeeper2::Status::Type n)
    {
        Detach(b, Stage::None);
        b.m_Tx.OnFailed(BaseTransaction::KeyKeeperErrorToFailureReason(n), true);
    }

    void BaseTxBuilder::KeyKeeperHandler::OnAllDone(BaseTxBuilder& b)
    {
        Detach(b, Stage::Done);
        b.m_Tx.Update(); // may complete transaction
    }

    ///////////////////////////////////////
    // BaseTxBuilder::Coins
    void BaseTxBuilder::Coins::AddOffset(ECC::Scalar::Native& kOffs, const Key::IKdf::Ptr& pMasterKdf) const
    {
        ECC::Scalar::Native sk;
        for (const CoinID& cid : m_Input)
        {
            CoinID::Worker(cid).Create(sk, *cid.get_ChildKdf(pMasterKdf));
            kOffs += sk;
        }

        for (const auto& si : m_InputShielded)
        {
            si.get_SkOut(sk, si.m_Fee, *pMasterKdf);
            kOffs += sk;
        }

        kOffs = -kOffs;

        for (const CoinID& cid : m_Output)
        {
            CoinID::Worker(cid).Create(sk, *cid.get_ChildKdf(pMasterKdf));
            kOffs += sk;
        }

        kOffs = -kOffs;
    }


    ///////////////////////////////////////
    // BaseTxBuilder::Balance

    BaseTxBuilder::Balance::Balance(BaseTxBuilder& b)
        :m_Builder(b)
    {
    }

    void BaseTxBuilder::Balance::Add(const Coin::ID& cid, bool bOutp)
    {
        (bOutp ? m_Builder.m_Coins.m_Output : m_Builder.m_Coins.m_Input).push_back(cid);

        Entry& x = m_Map[cid.m_AssetID];
        if (bOutp)
            x.m_Value -= cid.m_Value;
        else
            x.m_Value += cid.m_Value;
    }

    void BaseTxBuilder::Balance::Add(const IPrivateKeyKeeper2::ShieldedInput& si)
    {
        m_Builder.m_Coins.m_InputShielded.push_back(si);

        m_Map[si.m_AssetID].m_Value += si.m_Value;
        m_Map[0].m_Value -= si.m_Fee;
    }

    void BaseTxBuilder::Balance::Add(const ShieldedTxo::ID& sid)
    {
        auto& fs = Transaction::FeeSettings::get(m_Builder.m_Height.m_Min);

        IPrivateKeyKeeper2::ShieldedInput si;
        Cast::Down<ShieldedTxo::ID>(si) = sid;
        si.m_Fee = fs.m_ShieldedInputTotal; // auto-fee for shielded inputs
        Add(si);
    }

    void BaseTxBuilder::Balance::CreateOutput(Amount val, Asset::ID aid, Key::Type type)
    {
        Coin newUtxo = m_Builder.m_Tx.GetWalletDB()->generateNewCoin(val, aid);
        newUtxo.m_ID.m_Type = type;

        newUtxo.m_createTxId = m_Builder.m_Tx.GetTxID();
        m_Builder.m_Tx.GetWalletDB()->storeCoin(newUtxo);

        Add(newUtxo.m_ID, true);
    }

    void BaseTxBuilder::Balance::AddPreselected()
    {
        CoinIDList cidl;
        m_Builder.GetParameter(TxParameterID::PreselectedCoins, cidl);

        for (const auto& cid : cidl)
            Add(cid, false);
    }

    void BaseTxBuilder::Balance::CompleteBalance()
    {
        std::set<Asset::ID> setRcv; // those auto-created coins should be 'Regular' instead of 'Change'
        for (const auto& v : m_Map)
        {
            if (v.second.m_Value > 0)
                setRcv.insert(v.first);
        }

        AddPreselected();

        uint32_t nNeedInputs = 0;
        for (const auto& v : m_Map)
        {
            if (v.second.m_Value < 0)
                nNeedInputs++;
        }

        if (nNeedInputs)
        {
            if (m_Map[0].m_Value >= 0)
                nNeedInputs++; // although there's no dificiency in def asset, assume it may arise due to involuntary fees

            // go by asset type in reverse order, to reach the def asset last
            for (auto it = m_Map.rbegin(); m_Map.rend() != it; ++it)
            {
                AmountSigned& val = it->second.m_Value;
                if (val >= 0)
                    continue;

                uint32_t nShieldedMax = Rules::get().Shielded.MaxIns;
                uint32_t nShieldedInUse = static_cast<uint32_t>(m_Builder.m_Coins.m_InputShielded.size()); // preselected?

                if (nShieldedMax <= nShieldedInUse)
                    nShieldedMax = 0;
                else
                    nShieldedMax -= nShieldedInUse;

                assert(nNeedInputs);
                nShieldedMax = (nShieldedMax + nNeedInputs - 1) / nNeedInputs; // leave some reserve to other assets

                Asset::ID aid = it->first;

                std::vector<Coin> vSelStd;
                std::vector<ShieldedCoin> vSelShielded;
                m_Builder.m_Tx.GetWalletDB()->selectCoins2(m_Builder.m_Height.m_Min, -val, aid, vSelStd, vSelShielded, nShieldedMax, true);

                for (const auto& c : vSelStd)
                    Add(c.m_ID, false);

                for (const auto& c : vSelShielded)
                    Add(c.m_CoinID);

                if (val < 0)
                {
                    LOG_ERROR()
                        << m_Builder.m_Tx.GetTxID()
                        << "[" << m_Builder.m_SubTxID << "]"
                        << " Missing "
                        << PrintableAmount(Amount(-val), false, aid);

                    throw TransactionFailedException(!m_Builder.m_Tx.IsInitiator(), TxFailureReason::NoInputs);
                }

                nNeedInputs--;
            }

        }

        for (const auto& v : m_Map)
        {
            AmountSigned val = v.second.m_Value;
            assert(val >= 0);

            if (val > 0)
            {
                Asset::ID aid = v.first;

                bool bRcv = setRcv.end() != setRcv.find(aid);
                CreateOutput(val, aid, bRcv ? Key::Type::Regular : Key::Type::Change);
            }

            assert(!v.second.m_Value);
        }
    }

    ///////////////////////////////////////
    // BaseTxBuilder

    BaseTxBuilder::BaseTxBuilder(BaseTransaction& tx, SubTxID subTxID)
        :m_Tx(tx)
        ,m_SubTxID(subTxID)
    {
        GetParameter(TxParameterID::MinHeight, m_Height.m_Min);
        if (!m_Height.m_Min)
        {
            // automatically set current height
            Block::SystemState::Full s;
            if (m_Tx.GetTip(s))
                SaveAndStore(m_Height.m_Min, TxParameterID::MinHeight, s.m_Height);

        }

        GetParameter(TxParameterID::InputCoins, m_Coins.m_Input);
        GetParameter(TxParameterID::InputCoinsShielded, m_Coins.m_InputShielded);
        GetParameter(TxParameterID::OutputCoins, m_Coins.m_Output);

        m_pTransaction = std::make_shared<Transaction>();
        auto& trans = *m_pTransaction; // alias

        GetParameter(TxParameterID::Inputs, trans.m_vInputs);
        GetParameter(TxParameterID::ExtraKernels, trans.m_vKernels);
        GetParameter(TxParameterID::Outputs, trans.m_vOutputs);

        if (!GetParameter(TxParameterID::Offset, trans.m_Offset))
            trans.m_Offset = Zero;

        GetParameter(TxParameterID::MaxHeight, m_Height.m_Max);
        GetParameter(TxParameterID::Fee, m_Fee);

        bool bNoInOuts = trans.m_vInputs.empty() && trans.m_vOutputs.empty() && !wallet::GetShieldedInputsNum(trans.m_vKernels);
        m_GeneratingInOuts = bNoInOuts ? Stage::None : Stage::Done;

        GetParameter(TxParameterID::MutualTxState, m_Status);

        TxKernel::Ptr pKrn;
        GetParameter(TxParameterID::Kernel, pKrn);
        if (pKrn)
        {
            AddKernel(std::move(pKrn));

            if (Status::FullTx == m_Status)
                trans.NormalizeE(); // everything else must have already been normalized
        }
    }

    void BaseTxBuilder::SaveCoins()
    {
        bool bAssets = false;

        for (const auto& cid : m_Coins.m_Input)
            if (cid.m_AssetID)
                bAssets = true;

        for (const auto& si : m_Coins.m_InputShielded)
            if (si.m_AssetID)
                bAssets = true;

        for (const auto& cid : m_Coins.m_Output)
            if (cid.m_AssetID)
                bAssets = true;

        if (bAssets)
            VerifyAssetsEnabled();

        SetParameter(TxParameterID::InputCoins, m_Coins.m_Input);
        SetParameter(TxParameterID::InputCoinsShielded, m_Coins.m_InputShielded);
        SetParameter(TxParameterID::OutputCoins, m_Coins.m_Output);

        // tag inputs
        for (const auto& cid : m_Coins.m_Input)
        {
            Coin coin;
            coin.m_ID = cid;
            if (m_Tx.GetWalletDB()->findCoin(coin) && coin.m_status == Coin::Status::Available)
            {
                coin.m_spentTxId = m_Tx.GetTxID();
                m_Tx.GetWalletDB()->saveCoin(coin);
            }
            else
            {
                auto message("Coin with ID: " + toString(coin.m_ID) + " is unreachable.");
                LOG_ERROR() << message;
                throw TransactionFailedException(true, TxFailureReason::NoInputs, message.c_str());
            }
        }

        for (auto& cid : m_Coins.m_InputShielded)
        {
            auto pCoin = m_Tx.GetWalletDB()->getShieldedCoin(cid.m_Key);
            if (pCoin)
            {
                pCoin->m_spentTxId = m_Tx.GetTxID();
                m_Tx.GetWalletDB()->saveShieldedCoin(*pCoin);
            }
        }
    }

    bool BaseTxBuilder::IsGeneratingInOuts() const
    {
        return (Stage::InProgress == m_GeneratingInOuts);
    }

    bool BaseTxBuilder::IsSigning() const
    {
        return (Stage::InProgress == m_Signing);
    }

    template <typename TDst, typename TSrc>
    void MoveIntoVec1(std::vector<TDst>& vDst, std::vector<TSrc>& vSrc)
    {
        std::move(vSrc.begin(), vSrc.end(), back_inserter(vDst));
    }

    template <typename T>
    void MoveIntoVec(std::vector<T>& vDst, std::vector<T>& vSrc)
    {
        if (vDst.empty())
            vDst = std::move(vSrc);
        else
            MoveIntoVec1(vDst, vSrc);
    }

    struct BaseTxBuilder::HandlerInOuts
    {
        struct Shared
            :public std::enable_shared_from_this<Shared>
        {
            typedef std::shared_ptr<Shared> Ptr;

            TxVectors::Full m_InOuts;
            uint32_t m_Remaining;
        };

        struct Base
            :public KeyKeeperHandler
        {
            Base(BaseTxBuilder& b, Stage& s, const Shared::Ptr& pShared)
                :KeyKeeperHandler(b, s)
            {
                m_pShared = pShared;
                pShared->m_Remaining++;
            }

            using KeyKeeperHandler::KeyKeeperHandler;

            Shared::Ptr m_pShared;

            virtual bool OnDoneItem(BaseTxBuilder& b) = 0;

            void OnSuccess(BaseTxBuilder& b) override
            {
                if (OnDoneItem(b))
                {
                    auto& s = *m_pShared;
                    assert(s.m_Remaining);
                    if (!--s.m_Remaining)
                    {
                        MoveIntoVec(b.m_pTransaction->m_vOutputs, s.m_InOuts.m_vOutputs);
                        MoveIntoVec(b.m_pTransaction->m_vInputs, s.m_InOuts.m_vInputs);
                        MoveIntoVec1(b.m_pTransaction->m_vKernels, s.m_InOuts.m_vKernels);

                        b.SaveInOuts();

                        OnAllDone(b);
                    }
                    else
                        m_pStage = nullptr; // detach silently
                }
                else
                    OnFailed(b, IPrivateKeyKeeper2::Status::Unspecified); // although shouldn't happen
            }
        };

        struct HandlerOutput
            :public Base
            ,public std::enable_shared_from_this<HandlerOutput>
        {
            using Base::Base;

            virtual ~HandlerOutput() {} // auto

            IPrivateKeyKeeper2::Method::CreateOutput m_Method;

            bool OnDoneItem(BaseTxBuilder& b) override
            {
                assert(m_Method.m_pResult);
                m_pShared->m_InOuts.m_vOutputs.push_back(std::move(m_Method.m_pResult));
                return true;

            }
        };

        struct HandlerInput
            :public Base
            ,public std::enable_shared_from_this<HandlerInput>
        {
            using Base::Base;

            virtual ~HandlerInput() {} // auto

            IPrivateKeyKeeper2::Method::get_Commitment m_Method;

            bool OnDoneItem(BaseTxBuilder& b) override
            {
                auto& s = *m_pShared;
                s.m_InOuts.m_vInputs.emplace_back();
                s.m_InOuts.m_vInputs.back().reset(new Input);
                s.m_InOuts.m_vInputs.back()->m_Commitment = m_Method.m_Result;
                return true;
            }
        };

        struct HandlerInputShielded
            :public Base
            ,public std::enable_shared_from_this<HandlerInputShielded>
        {
            using Base::Base;

            virtual ~HandlerInputShielded() {} // auto


            struct MyList
                :public Sigma::CmList
            {
                std::vector<ECC::Point::Storage> m_vec;

                ECC::Point::Storage* m_p0;
                uint32_t m_Skip;

                virtual bool get_At(ECC::Point::Storage& res, uint32_t iIdx) override
                {
                    if (iIdx < m_Skip)
                    {
                        res.m_X = Zero;
                        res.m_Y = Zero;
                    }
                    else
                        res = m_p0[iIdx - m_Skip];

                    return true;
                }
            };

            IPrivateKeyKeeper2::Method::CreateInputShielded m_Method;
            MyList m_Lst;

            TxoID m_Wnd0;
            uint32_t m_N;
            uint32_t m_Count;

            bool OnDoneItem(BaseTxBuilder& b) override
            {
                assert(m_Method.m_pKernel);
                m_pShared->m_InOuts.m_vKernels.push_back(std::move(m_Method.m_pKernel));
                return true;
            }

            bool InvokeSafe(BaseTxBuilder&, const IPrivateKeyKeeper2::ShieldedInput&);
            bool OnList(BaseTxBuilder&, proto::ShieldedList& msg);

        };
    };

    struct MyWrapper
    {
        const TxKernel::Ptr* m_p0;
        size_t m_Count;
        TxKernel* m_pSkip;

        template <typename Archive>
        void serialize(Archive& ar)
        {
            size_t n = m_Count - (!!m_pSkip);
            ar & n;

            for (size_t i = 0; i < m_Count; i++)
            {
                const auto& pKrn = m_p0[i];
                if (pKrn.get() != m_pSkip)
                    ar & pKrn;
            }
        }
    };

    void BaseTxBuilder::SaveInOuts()
    {
        SetParameter(TxParameterID::Inputs, m_pTransaction->m_vInputs);
        SetParameter(TxParameterID::Outputs, m_pTransaction->m_vOutputs);

        // serialize kernels, except the 'main' one
        const auto& v = m_pTransaction->m_vKernels;

        MyWrapper wr;
        wr.m_pSkip = m_pKrn;
        wr.m_Count = v.size();
        if (wr.m_Count)
            wr.m_p0 = &v.front();


        SetParameter(TxParameterID::ExtraKernels, wr);

    }

    void BaseTxBuilder::GenerateInOuts()
    {
        if (Stage::None != m_GeneratingInOuts)
            return;

        if (m_Coins.IsEmpty())
        {
            m_GeneratingInOuts = Stage::Done;
            return;
        }

        auto pShared = std::make_shared<HandlerInOuts::Shared>();
        pShared->m_Remaining = 0;

        // outputs
        for (const auto& cid : m_Coins.m_Output)
        {
            m_GeneratingInOuts = Stage::None;
            auto pHandler = std::make_shared<HandlerInOuts::HandlerOutput>(*this, m_GeneratingInOuts, pShared);
            auto& h = *pHandler;

            h.m_Method.m_hScheme = m_Height.m_Min;
            h.m_Method.m_Cid = cid;
            FillUserData(Output::User::ToPacked(h.m_Method.m_User));
            m_Tx.get_KeyKeeperStrict()->InvokeAsync(h.m_Method, pHandler);
        }

        // inputs
        for (const auto& cid : m_Coins.m_Input)
        {
            m_GeneratingInOuts = Stage::None;
            auto pHandler = std::make_shared<HandlerInOuts::HandlerInput>(*this, m_GeneratingInOuts, pShared);
            auto& h = *pHandler;

            h.m_Method.m_Cid = cid;
            m_Tx.get_KeyKeeperStrict()->InvokeAsync(h.m_Method, pHandler);
        }

        // inputs shielded
        for (const auto& ins : m_Coins.m_InputShielded)
        {
            m_GeneratingInOuts = Stage::None;
            auto pHandler = std::make_shared<HandlerInOuts::HandlerInputShielded>(*this, m_GeneratingInOuts, pShared);
            auto& h = *pHandler;

            if (!h.InvokeSafe(*this, ins))
                throw TransactionFailedException(true, TxFailureReason::Unknown);
        }

    }

    bool BaseTxBuilder::HandlerInOuts::HandlerInputShielded::InvokeSafe(BaseTxBuilder& b, const IPrivateKeyKeeper2::ShieldedInput& si)
    {
        // currently process inputs 1-by-1
        // don't cache retrieved elements (i.e. ignore overlaps for multiple inputs)
        auto pCoin = b.m_Tx.GetWalletDB()->getShieldedCoin(si.m_Key);
        if (!pCoin)
            return false;
        const ShieldedCoin& c = *pCoin;

        Cast::Down<ShieldedTxo::ID>(m_Method) = c.m_CoinID;

        m_Method.m_pKernel = std::make_unique<TxKernelShieldedInput>();
        m_Method.m_pKernel->m_Fee = si.m_Fee;

        TxoID nShieldedCurrently = b.m_Tx.GetWalletDB()->get_ShieldedOuts();
        std::setmax(nShieldedCurrently, c.m_TxoID + 1); // assume stored shielded count may be inaccurate, the being-spent element must be present

        ShieldedCoin::UnlinkStatus us(c, nShieldedCurrently);

        bool bWndLost = us.IsLargeSpendWindowLost();
        m_Method.m_pKernel->m_SpendProof.m_Cfg = bWndLost ?
            Rules::get().Shielded.m_ProofMin :
            Rules::get().Shielded.m_ProofMax;

        m_N = m_Method.m_pKernel->m_SpendProof.m_Cfg.get_N();
        if (!m_N)
            return false;

        m_Method.m_iIdx = c.get_WndIndex(m_N);

        if (!bWndLost && (us.m_WndReserve0 < 0))
        {
            // move the selected window forward
            m_Method.m_iIdx += us.m_WndReserve0; // actually decrease
            assert(m_Method.m_iIdx < m_N);
        }

        m_Wnd0 = c.m_TxoID - m_Method.m_iIdx;
        m_Count = m_N;

        TxoID nWndEnd = m_Wnd0 + m_N;
        if (nWndEnd > nShieldedCurrently)
        {
            // move the selected window backward
            uint32_t nExtra = static_cast<uint32_t>(nWndEnd - nShieldedCurrently);
            if (nExtra < m_Wnd0)
                m_Wnd0 -= nExtra;
            else
            {
                nExtra = static_cast<uint32_t>(m_Wnd0);
                m_Wnd0 = 0;
            }

            m_Method.m_iIdx += nExtra;
            m_Count += nExtra;
        }

        LOG_INFO() << "ShieldedInput window N=" << m_N << ", Wnd0=" << m_Wnd0 << ", iIdx=" << m_Method.m_iIdx << ", TxoID=" << c.m_TxoID << ", PoolSize=" << nShieldedCurrently;

        b.m_Tx.GetGateway().get_shielded_list(b.m_Tx.GetTxID(), m_Wnd0, m_Count,
            [pHandler = shared_from_this(), weakTx = b.m_Tx.weak_from_this()](TxoID, uint32_t, proto::ShieldedList& msg)
        {
            auto pBldr = pHandler->m_pBuilder.lock();
            if (!pBldr)
                return;
            BaseTxBuilder& b = *pBldr;

            auto pTx = weakTx.lock();
            if (!pTx)
                return;

            try {
                if (!pHandler->OnList(b, msg))
                    b.m_Tx.OnFailed(TxFailureReason::Unknown);
            }
            catch (const TransactionFailedException& ex) {
                b.m_Tx.OnFailed(ex.GetReason(), ex.ShouldNofify());
            }
        });

        return true;
    }

    bool BaseTxBuilder::HandlerInOuts::HandlerInputShielded::OnList(BaseTxBuilder& b, proto::ShieldedList& msg)
    {
        if (msg.m_Items.size() > m_Count)
        {
            LOG_ERROR() << "ShieldedList message returned more coins than requested " << TRACE(msg.m_Items.size()) << TRACE(m_Count);
            return false;
        }

        uint32_t nItems = static_cast<uint32_t>(msg.m_Items.size());

        m_Lst.m_p0 = &msg.m_Items.front();
        m_Lst.m_Skip = 0;
        m_Lst.m_vec.swap(msg.m_Items);

        m_Method.m_pList = &m_Lst;
        m_Method.m_pKernel->m_NotSerialized.m_hvShieldedState = msg.m_State1;

        m_Method.m_pKernel->m_Height = b.m_Height;
        m_Method.m_pKernel->m_WindowEnd = m_Wnd0 + nItems;

        if (nItems > m_N)
        {
            uint32_t nDelta = nItems - m_N;
            m_Lst.m_p0 += nDelta;

            assert(m_Method.m_iIdx >= nDelta);
            m_Method.m_iIdx -= nDelta;
        }

        if (nItems < m_N)
        {
            if (m_Wnd0 || (nItems <= m_Method.m_iIdx))
            {
                LOG_ERROR() << "ShieldedList message returned unexpected data " << TRACE(m_Wnd0) << TRACE(nItems) << TRACE(m_Method.m_iIdx);
                return false;
            }

            uint32_t nDelta = m_N - nItems;
            m_Lst.m_Skip = nDelta;

            m_Method.m_iIdx += nDelta;
            assert(m_Method.m_iIdx < m_N);
        }

        b.m_Tx.get_KeyKeeperStrict()->InvokeAsync(m_Method, shared_from_this());

        return true;
    }

    void BaseTxBuilder::AddOffset(const ECC::Scalar& k1)
    {
        AddOffset(ECC::Scalar::Native(k1));
    }

    void BaseTxBuilder::AddOffset(const ECC::Scalar::Native& k1)
    {
        ECC::Scalar::Native k(m_pTransaction->m_Offset);
        k += k1;
        m_pTransaction->m_Offset = k;

        SetParameter(TxParameterID::Offset, m_pTransaction->m_Offset);
    }

    bool BaseTxBuilder::Aggregate(ECC::Point& ptDst, const ECC::Point::Native& ptSrcN)
    {
        ECC::Point::Native pt;
        if (!pt.Import(ptDst))
            return false;

        pt += ptSrcN;
        pt.Export(ptDst);
        return true;
    }

    bool BaseTxBuilder::Aggregate(ECC::Point& ptDst, ECC::Point::Native& ptSrcN, const ECC::Point& ptSrc)
    {
        return
            ptSrcN.Import(ptSrc) &&
            Aggregate(ptDst, ptSrcN);
    }

    void BaseTxBuilder::SaveKernel()
    {
        // this is ugly hack, but should work without extra code generated
        SetParameter(TxParameterID::Kernel, Cast::Reinterpret<TxKernel::Ptr>(m_pKrn));
    }

    void BaseTxBuilder::SaveKernelID()
    {
        assert(m_pKrn);
        SetParameter(TxParameterID::KernelID, m_pKrn->m_Internal.m_ID);
    }

    void BaseTxBuilder::SetInOuts(IPrivateKeyKeeper2::Method::TxCommon& m)
    {
        m.m_vInputs = m_Coins.m_Input;
        m.m_vOutputs = m_Coins.m_Output;
        m.m_vInputsShielded = m_Coins.m_InputShielded;

        m.m_NonConventional = !IsConventional();
    }

    void BaseTxBuilder::SetCommon(IPrivateKeyKeeper2::Method::TxCommon& m)
    {
        BaseTxBuilder::SetInOuts(m);
        m.m_pKernel.reset(new TxKernelStd);
        m.m_pKernel->m_Fee = m_Fee;
        m.m_pKernel->m_Height = m_Height;
    }

    void BaseTxBuilder::VerifyTx()
    {
        TxBase::Context ctx;
        ctx.m_Height.m_Min = m_Height.m_Min;
        // TODO:DEX set to false, other side will not see it!!!

        std::string sErr;
        if (!m_pTransaction->IsValid(ctx, &sErr))
            throw TransactionFailedException(false, TxFailureReason::InvalidTransaction, sErr.c_str());
    }

    void BaseTxBuilder::VerifyAssetsEnabled()
    {
        TxFailureReason res = CheckAssetsEnabled(m_Height.m_Min);
        if (TxFailureReason::Count != res)
            throw TransactionFailedException(!m_Tx.IsInitiator(), res);
    }

    bool BaseTxBuilder::SignTx()
    {
        return true;
    }

    void BaseTxBuilder::FinalyzeTx()
    {
        if (Status::FullTx == m_Status)
            return;

        assert(!IsGeneratingInOuts());

        FinalizeTxInternal();
    }

    void BaseTxBuilder::FinalizeTxInternal()
    {
        m_pTransaction->Normalize();
        VerifyTx();

        SaveInOuts();

        SetStatus(Status::FullTx);
    }

    void BaseTxBuilder::AddKernel(TxKernel::Ptr&& pKrn)
    {
        m_pKrn = pKrn.get();
        m_pTransaction->m_vKernels.push_back(std::move(pKrn));
    }

    void BaseTxBuilder::SetStatus(Status::Type s)
    {
        SaveAndStore(m_Status, TxParameterID::MutualTxState, s);
    }

    void BaseTxBuilder::FillUserData(Output::User::Packed* user)
    {
        user->m_TxID = Blob(m_Tx.GetTxID().data(), (uint32_t)m_Tx.GetTxID().size());
        user->m_Fee = m_Fee;
    }

    string BaseTxBuilder::GetKernelIDString() const
    {
        Merkle::Hash kernelID;
        GetParameter(TxParameterID::KernelID, kernelID);
        char sz[Merkle::Hash::nTxtLen + 1];
        kernelID.Print(sz);
        return string(sz);
    }

    void BaseTxBuilder::CheckMinimumFee(const TxStats* pFromPeer /* = nullptr */)
    {
        // after 1st fork fee should be >= minimal fee
        if (Rules::get().pForks[1].m_Height <= m_Height.m_Min)
        {
            Amount feeInps = 0;
            for (const auto& si : m_Coins.m_InputShielded)
                feeInps += si.m_Fee; // account for shielded inputs, they carry their fee individually

            TxStats ts;
            ts.m_Outputs = static_cast<uint32_t>(m_Coins.m_Output.size());
            ts.m_InputsShielded = static_cast<uint32_t>(m_Coins.m_InputShielded.size());
            ts.m_Kernels = 1 + static_cast<uint32_t>(m_Coins.m_InputShielded.size());

            if (pFromPeer)
                ts += *pFromPeer;

            auto& fs = Transaction::FeeSettings::get(m_Height.m_Min);
            Amount minFee = fs.Calculate(ts);

            if (m_Fee + feeInps < minFee)
            {
                stringstream ss;
                ss << "The minimum fee must be: " << (minFee - feeInps) << " .";
                throw TransactionFailedException(false, TxFailureReason::FeeIsTooSmall, ss.str().c_str());
            }
        }
    }


    ///////////////////////////////////////
    // SimpleTxBuilder

    SimpleTxBuilder::SimpleTxBuilder(BaseTransaction& tx, SubTxID subTxID)
        : BaseTxBuilder(tx, subTxID)
        , m_Lifetime(kDefaultTxLifetime)
    {
        GetParameter(TxParameterID::Amount, m_Amount);
        GetParameter(TxParameterID::AssetID, m_AssetID);
        GetParameter(TxParameterID::Lifetime, m_Lifetime);
    }

    void SimpleTxBuilder::SignSplit()
    {
        if (Stage::None != m_Signing)
            return;

        struct MyHandler
            :public KeyKeeperHandler
        {
            using KeyKeeperHandler::KeyKeeperHandler;

            IPrivateKeyKeeper2::Method::SignSplit m_Method;

            virtual ~MyHandler() {} // auto

            virtual void OnSuccess(BaseTxBuilder& b_) override
            {
                SimpleTxBuilder& b = Cast::Up<SimpleTxBuilder>(b_);
                b.AddOffset(m_Method.m_kOffset);
                b.AddKernel(std::move(m_Method.m_pKernel));
                b.SaveKernel();
                b.SaveKernelID();
                b.SetStatus(Status::SelfSigned);

                OnAllDone(b);
            }
        };

        KeyKeeperHandler::Ptr pHandler = std::make_shared<MyHandler>(*this, m_Signing);
        MyHandler& x = Cast::Up<MyHandler>(*pHandler);

        SetCommon(x.m_Method);
        m_Tx.get_KeyKeeperStrict()->InvokeAsync(x.m_Method, pHandler);
    }

    bool SimpleTxBuilder::SignTx()
    {
        GenerateInOuts();
        SignSplit();

        return (m_Status >= Status::SelfSigned) && !IsGeneratingInOuts();
    }

    void SimpleTxBuilder::FillUserData(Output::User::Packed* user)
    {
        BaseTxBuilder::FillUserData(user);
        user->m_Amount = m_Amount;
    }

    ///////////////////////////////////////
    // MutualTxBuilder

    MutualTxBuilder::MutualTxBuilder(BaseTransaction& tx, SubTxID subTxID)
        :SimpleTxBuilder(tx, subTxID)
    {
        GetParameter(TxParameterID::IsSender, m_IsSender);

        Height responseTime = 0;
        Height responseHeight = 0;
        if (GetParameter(TxParameterID::PeerResponseTime, responseTime)
          && !GetParameter(TxParameterID::PeerResponseHeight, responseHeight))
        {
            auto currentHeight = m_Tx.GetWalletDB()->getCurrentHeight();
            // adjust response height, if min height did not set then it should be equal to responce time
            SetParameter(TxParameterID::PeerResponseHeight, responseTime + currentHeight);
        }
    }

    void MutualTxBuilder::CreateKernel(TxKernelStd::Ptr& pKrn)
    {
        pKrn = make_unique<TxKernelStd>();
        pKrn->m_Fee = m_Fee;
        pKrn->m_Height.m_Min = m_Height.m_Min;
        pKrn->m_Height.m_Max = m_Height.m_Max;
        pKrn->m_Commitment = Zero;
        ZeroObject(pKrn->m_Signature);

        // load kernel's extra data
        Hash::Value hv;
        if (GetParameter(TxParameterID::PeerLockImage, hv))
        {
			pKrn->m_pHashLock = make_unique<TxKernelStd::HashLock>();
			pKrn->m_pHashLock->m_IsImage = true;
			pKrn->m_pHashLock->m_Value = hv;
        }

        if (GetParameter(TxParameterID::PreImage, hv))
        {
			pKrn->m_pHashLock = make_unique<TxKernelStd::HashLock>();
			pKrn->m_pHashLock->m_Value = hv;
		}
    }

    void MutualTxBuilder::AddPeerSignature(const ECC::Point::Native& ptNonce, const ECC::Point::Native& ptExc)
    {
        GetParameterStrict(TxParameterID::PeerSignature, m_pKrn->CastTo_Std().m_Signature.m_k);

        if (!m_pKrn->CastTo_Std().m_Signature.IsValidPartial(m_pKrn->m_Internal.m_ID, ptNonce, ptExc))
            throw TransactionFailedException(true, TxFailureReason::InvalidPeerSignature);

    }

    bool MutualTxBuilder::LoadPeerPart(ECC::Point::Native& ptNonce, ECC::Point::Native& ptExc)
    {
        ECC::Point pt;
        return
            GetParameter(TxParameterID::PeerPublicNonce, pt) &&
            ptNonce.Import(pt) &&
            GetParameter(TxParameterID::PeerPublicExcess, pt) &&
            ptExc.Import(pt);
    }

    void MutualTxBuilder::AddPeerOffset()
    {
        ECC::Scalar k;
        if (GetParameter(TxParameterID::PeerOffset, k))
            AddOffset(k);
    }

    void MutualTxBuilder::FinalizeTxInternal()
    {
        // add peer in/out/offs
        AddPeerOffset();

        std::vector<Input::Ptr> vIns;
        if (GetParameter(TxParameterID::PeerInputs, vIns))
            MoveIntoVec(m_pTransaction->m_vInputs, vIns);

        std::vector<Output::Ptr> vOuts;
        if (GetParameter(TxParameterID::PeerOutputs, vOuts))
            MoveIntoVec(m_pTransaction->m_vOutputs, vOuts);

        SimpleTxBuilder::FinalizeTxInternal();
    }

    void MutualTxBuilder::PrepareMutualParams(IPrivateKeyKeeper2::Method::TxMutual& m)
    {
        m.m_Peer = Zero;
        m.m_iEndpoint = 0;

        if (!m.m_NonConventional)
        {
            GetParameterStrict(TxParameterID::MyAddressID, m.m_iEndpoint);

            PeerID epMy;
            bool bNewerScheme = GetParameter(TxParameterID::PeerEndpoint, m.m_Peer) && GetParameter(TxParameterID::MyEndpoint, epMy);
            if (!bNewerScheme)
            {
                // legacy. Will fail for trustless key keeper.
                m.m_IsBbs = true;

                WalletID wid;
                GetParameterStrict(TxParameterID::PeerAddr, wid);
                m.m_Peer = wid.m_Pk;
            }
        }

    }

    void MutualTxBuilder::SignSender(bool initial)
    {
        if (Stage::InProgress == m_Signing)
            return;
        m_Signing = Stage::None;

        struct MyHandler
            :public KeyKeeperHandler
        {
            using KeyKeeperHandler::KeyKeeperHandler;

            IPrivateKeyKeeper2::Method::SignSender m_Method;

            virtual ~MyHandler() {} // auto

            virtual void OnSuccess(BaseTxBuilder& b_) override
            {
                MutualTxBuilder& b = Cast::Up<MutualTxBuilder>(b_);

                if (b.m_pKrn)
                {
                    // final, update the signature only
                    b.m_pKrn->CastTo_Std().m_Signature.m_k = m_Method.m_pKernel->m_Signature.m_k;
                    b.AddOffset(m_Method.m_kOffset);

                    b.m_Tx.FreeSlotSafe(); // release it ASAP

                    b.SetStatus(Status::SndFull);
                }
                else
                {
                    // initial
                    b.SetParameter(TxParameterID::UserConfirmationToken, m_Method.m_UserAgreement);

                    b.AddKernel(std::move(m_Method.m_pKernel));
                    b.SetStatus(Status::SndHalf);
                }

                b.SaveKernel();
                OnAllDone(b);
            }
        };

        KeyKeeperHandler::Ptr pHandler = std::make_shared<MyHandler>(*this, m_Signing);
        MyHandler& x = Cast::Up<MyHandler>(*pHandler);
        IPrivateKeyKeeper2::Method::SignSender& m = x.m_Method;

        SetInOuts(m);

        m.m_Slot = m_Tx.GetSlotSafe(true);

        PrepareMutualParams(m);

        ZeroObject(m.m_PaymentProofSignature);
        m.m_UserAgreement = Zero;

        if (initial)
            CreateKernel(m.m_pKernel);
        else
        {
            m_pKrn->Clone(Cast::Reinterpret<TxKernel::Ptr>(m.m_pKernel));

            GetParameter(TxParameterID::UserConfirmationToken, m.m_UserAgreement);
            if (m.m_UserAgreement == Zero)
                throw TransactionFailedException(true, TxFailureReason::FailedToGetParameter);

            GetParameter(TxParameterID::PaymentConfirmation, m.m_PaymentProofSignature);
        }


        m_Tx.get_KeyKeeperStrict()->InvokeAsync(x.m_Method, pHandler);
    }

    void MutualTxBuilder::SignReceiver()
    {
        if (Stage::InProgress == m_Signing)
            return;
        m_Signing = Stage::None;

        struct MyHandler
            :public KeyKeeperHandler
        {
            using KeyKeeperHandler::KeyKeeperHandler;

            IPrivateKeyKeeper2::Method::SignReceiver m_Method;

            virtual ~MyHandler() {} // auto

            void AssignExtractDiff(BaseTxBuilder& b, ECC::Point& dst, const ECC::Point& src, TxParameterID par)
            {
                dst.m_Y ^= 1;

                ECC::Point::Native pt;
                b.Aggregate(dst, pt, src);
                b.SetParameter(par, dst);

                dst = src;
            }

            virtual void OnSuccess(BaseTxBuilder& b_) override
            {
                MutualTxBuilder& b = Cast::Up<MutualTxBuilder>(b_);

                AssignExtractDiff(b, b.m_pKrn->CastTo_Std().m_Commitment, m_Method.m_pKernel->m_Commitment, TxParameterID::PublicExcess);
                AssignExtractDiff(b, b.m_pKrn->CastTo_Std().m_Signature.m_NoncePub, m_Method.m_pKernel->m_Signature.m_NoncePub, TxParameterID::PublicNonce);
                b.m_pKrn->CastTo_Std().m_Signature.m_k = m_Method.m_pKernel->m_Signature.m_k;

                b.m_pKrn->UpdateID();
                b.SaveKernel();
                b.SaveKernelID();
                b.SetStatus(Status::RcvFullHalfSig);

                b.AddOffset(m_Method.m_kOffset);

                if (m_Method.m_iEndpoint)
                    b.SetParameter(TxParameterID::PaymentConfirmation, m_Method.m_PaymentProofSignature);

                OnAllDone(b);
            }
        };

        KeyKeeperHandler::Ptr pHandler = std::make_shared<MyHandler>(*this, m_Signing);
        MyHandler& x = Cast::Up<MyHandler>(*pHandler);
        IPrivateKeyKeeper2::Method::SignReceiver& m = x.m_Method;

        SetInOuts(m);
        m_pKrn->Clone(Cast::Reinterpret<TxKernel::Ptr>(m.m_pKernel));

        PrepareMutualParams(m);

        m_Tx.get_KeyKeeperStrict()->InvokeAsync(x.m_Method, pHandler);
    }

    void MutualTxBuilder::FinalyzeMaxHeight()
    {
        if (MaxHeight != m_Height.m_Max)
            return; // already decided

        GetParameter(TxParameterID::PeerMaxHeight, m_Height.m_Max);
        GetParameter(TxParameterID::Lifetime, m_Lifetime); // refresh it too

        // receiver is allowed to adjust what was suggested by the sender
        if (!m_IsSender && m_Lifetime)
        {
            // we're allowed to adjust peer's sugggested max height
            Block::SystemState::Full s;
            if (m_Tx.GetTip(s))
                m_Height.m_Max = s.m_Height + m_Lifetime;
        }

        // sanity check
        Height hPeerResponce = 0;
        GetParameter(TxParameterID::PeerResponseHeight, hPeerResponce);

        if (hPeerResponce && (m_Height.m_Max > m_Lifetime + hPeerResponce))
            throw TransactionFailedException(true, TxFailureReason::MaxHeightIsUnacceptable);

        SetParameter(TxParameterID::MaxHeight, m_Height.m_Max);
    }


    bool MutualTxBuilder::SignTx()
    {
        GenerateInOuts();

        bool bRes = m_IsSender ?
            SignTxSender() :
            SignTxReceiver();

        if (!bRes)
            m_Tx.UpdateOnNextTip();

        return bRes;
    }

    bool MutualTxBuilder::SignTxSender()
    {
        switch (m_Status)
        {
        case Status::None:
            SignSender(true);
            break;

        case Status::SndHalf:
            {
                SetTxParameter msg;
                msg
                    .AddParameter(TxParameterID::PeerPublicExcess, m_pKrn->CastTo_Std().m_Commitment)
                    .AddParameter(TxParameterID::PeerPublicNonce, m_pKrn->CastTo_Std().m_Signature.m_NoncePub);

                if (m_pKrn->CastTo_Std().m_pHashLock)
                {
                    ECC::Hash::Value lockImage = m_pKrn->CastTo_Std().m_pHashLock->get_Image(lockImage);
                    msg.AddParameter(TxParameterID::PeerLockImage, lockImage);
                }

                SendToPeer(std::move(msg));

                SetStatus(Status::SndHalfSent);
            }
            // no break;

        case Status::SndHalfSent:
            {
                // check if receiver part is ready
                ECC::Point::Native ptNonce, ptExc;
                if (!LoadPeerPart(ptNonce, ptExc))
                    break;

                Aggregate(m_pKrn->CastTo_Std().m_Commitment, ptExc);
                Aggregate(m_pKrn->CastTo_Std().m_Signature.m_NoncePub, ptNonce);

                FinalyzeMaxHeight();
                m_pKrn->m_Height.m_Max = m_Height.m_Max; // can be different from the original

                m_pKrn->UpdateID(); // must be valid already
                SaveKernelID();

                AddPeerSignature(ptNonce, ptExc);

                SaveKernel();

                SetStatus(Status::SndFullHalfSig);

            }
            // no break;

        case Status::SndFullHalfSig:
            SignSender(false);
        }

        return (m_Status >= Status::SndFull) && !IsGeneratingInOuts();

    }

    bool MutualTxBuilder::SignTxReceiver()
    {
        switch (m_Status)
        {
        case Status::None:
            {
                ECC::Point ptNonce, ptExc;

                if (!GetParameter(TxParameterID::PeerPublicNonce, ptNonce) ||
                    !GetParameter(TxParameterID::PeerPublicExcess, ptExc))
                    break;

                FinalyzeMaxHeight();

                TxKernelStd::Ptr pKrn;
                CreateKernel(pKrn);
                pKrn->m_Commitment = ptExc;
                pKrn->m_Signature.m_NoncePub = ptNonce;

                AddKernel(std::move(pKrn));
                SaveKernel();

                SetStatus(Status::RcvHalf);
            }
            // no break;

        case Status::RcvHalf:
            SignReceiver();
            break;

        case Status::RcvFullHalfSig:
            {
                if (IsGeneratingInOuts())
                    break;

                SetTxParameter msg;
                msg
                    .AddParameter(TxParameterID::PeerPublicExcess, GetParameterStrict<ECC::Point>(TxParameterID::PublicExcess))
                    .AddParameter(TxParameterID::PeerPublicNonce, GetParameterStrict<ECC::Point>(TxParameterID::PublicNonce))
                    .AddParameter(TxParameterID::PeerSignature, m_pKrn->CastTo_Std().m_Signature.m_k)
                    .AddParameter(TxParameterID::PeerInputs, m_pTransaction->m_vInputs)
                    .AddParameter(TxParameterID::PeerOutputs, m_pTransaction->m_vOutputs)
                    .AddParameter(TxParameterID::PeerOffset, m_pTransaction->m_Offset);

                Signature sig;
                if (GetParameter(TxParameterID::PaymentConfirmation, sig))
                    msg.AddParameter(TxParameterID::PaymentConfirmation, sig);

                SendToPeer(std::move(msg));

                SetStatus(Status::RcvFullHalfSigSent);
            }

        }

        return (m_Status >= Status::RcvFullHalfSigSent);
    }
    
    void MutualTxBuilder::FillUserData(Output::User::Packed* user)
    {
        SimpleTxBuilder::FillUserData(user);
        PeerID peerID = Zero;
        m_Tx.GetParameter(TxParameterID::PeerEndpoint, peerID);
        user->m_Peer = peerID;
    }
}
