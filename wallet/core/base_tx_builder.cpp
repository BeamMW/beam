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

    BaseTxBuilder::BaseTxBuilder(BaseTransaction& tx, SubTxID subTxID)
        :m_Tx(tx)
        ,m_SubTxID(subTxID)
    {
        m_Tx.GetParameter(TxParameterID::MinHeight, m_Height.m_Min, m_SubTxID);
        if (!m_Height.m_Min)
        {
            // automatically set current height
            Block::SystemState::Full s;
            if (m_Tx.GetTip(s))
                SaveAndStore(m_Height.m_Min, TxParameterID::MinHeight, s.m_Height);

        }

        m_Tx.GetParameter(TxParameterID::InputCoins, m_Coins.m_Input, m_SubTxID);
        m_Tx.GetParameter(TxParameterID::InputCoinsShielded, m_Coins.m_InputShielded, m_SubTxID);
        m_Tx.GetParameter(TxParameterID::OutputCoins, m_Coins.m_Output, m_SubTxID);
        RefreshBalance();

        m_pTransaction = std::make_shared<Transaction>();

        m_Tx.GetParameter(TxParameterID::Inputs, m_pTransaction->m_vInputs, m_SubTxID);
        m_Tx.GetParameter(TxParameterID::InputsShielded, m_pTransaction->m_vKernels, m_SubTxID);
        m_Tx.GetParameter(TxParameterID::Outputs, m_pTransaction->m_vOutputs, m_SubTxID);

        if (!m_Tx.GetParameter(TxParameterID::Offset, m_pTransaction->m_Offset, m_SubTxID))
            m_pTransaction->m_Offset = Zero;

        m_Tx.GetParameter(TxParameterID::MaxHeight, m_Height.m_Max, m_SubTxID);
        m_Tx.GetParameter(TxParameterID::Fee, m_Fee, m_SubTxID);

        bool bEmpty = m_pTransaction->m_vInputs.empty() && m_pTransaction->m_vOutputs.empty() && m_pTransaction->m_vKernels.empty();
        m_GeneratingInOuts = bEmpty ? Stage::None : Stage::Done;
    }

    void BaseTxBuilder::Balance::Add(const Coin::ID& cid, bool bOutp)
    {
        Entry& x = m_Map[cid.m_AssetID];
        (bOutp ? x.m_Out : x.m_In) += cid.m_Value;
    }

    void BaseTxBuilder::Balance::Add(const IPrivateKeyKeeper2::ShieldedInput& si)
    {
        m_Map[si.m_AssetID].m_In += si.m_Value;
        m_Map[0].m_Out += si.m_Fee;
        m_Fees += si.m_Fee;
    }

    void BaseTxBuilder::AddOutput(const Coin::ID& cid)
    {
        m_Coins.m_Output.push_back(cid);
        m_Balance.Add(cid, true);
    }

    void BaseTxBuilder::CreateAddNewOutput(Coin::ID& cid)
    {
        Coin newUtxo = m_Tx.GetWalletDB()->generateNewCoin(cid.m_Value, cid.m_AssetID);
        newUtxo.m_ID.m_Type = cid.m_Type;

        newUtxo.m_createTxId = m_Tx.GetTxID();
        m_Tx.GetWalletDB()->storeCoin(newUtxo);

        cid = newUtxo.m_ID;
        AddOutput(cid);
    }

    void BaseTxBuilder::RefreshBalance()
    {
        m_Balance.m_Map.clear();
        m_Balance.m_Fees = 0;

        for (const auto& cid : m_Coins.m_Input)
            m_Balance.Add(cid, false);

        for (const auto& cid : m_Coins.m_Output)
            m_Balance.Add(cid, true);

        for (const auto& si : m_Coins.m_InputShielded)
            m_Balance.Add(si);
    }

    bool BaseTxBuilder::Balance::Entry::IsEnoughNetTx(Amount val) const
    {
        return (m_In >= m_Out) && (m_In - m_Out >= val);
    }

    void BaseTxBuilder::SaveCoins()
    {
        m_Tx.SetParameter(TxParameterID::InputCoins, m_Coins.m_Input, m_SubTxID);
        m_Tx.SetParameter(TxParameterID::InputCoinsShielded, m_Coins.m_InputShielded, m_SubTxID);
        m_Tx.SetParameter(TxParameterID::OutputCoins, m_Coins.m_Output, m_SubTxID);
    }

    Amount BaseTxBuilder::MakeInputsAndChange(Amount val, Asset::ID aid)
    {
        Amount v = MakeInputs(val, aid);
        if (v > val)
        {
            CoinID cid;
            cid.set_Subkey(0);
            cid.m_Value = v - val;
            cid.m_AssetID = aid;
            cid.m_Type = Key::Type::Change;

            CreateAddNewOutput(cid);
        }

        return v;
    }

    Amount BaseTxBuilder::MakeInputs(Amount val, Asset::ID aid)
    {
        Balance::Entry& x = m_Balance.m_Map[aid];
        MakeInputs(x, val, aid);
        return x.m_In - x.m_Out;
    }

    void BaseTxBuilder::MakeInputs(Balance::Entry& x, Amount val, Asset::ID aid)
    {
        if (x.IsEnoughNetTx(val))
            return;

        if (aid)
            VerifyAssetsEnabled();

        uint32_t nShieldedMax = Rules::get().Shielded.MaxIns;
        uint32_t nShieldedInUse = static_cast<uint32_t>(m_Coins.m_InputShielded.size());

        if (aid)
            nShieldedInUse += nShieldedMax / 2; // leave at least half for beams

        if (nShieldedMax <= nShieldedInUse)
            nShieldedMax = 0;
        else
            nShieldedMax -= nShieldedInUse;

        Transaction::FeeSettings fs;
        Amount feeShielded = fs.m_Kernel + fs.m_ShieldedInput;

        std::vector<Coin> vSelStd;
        std::vector<ShieldedCoin> vSelShielded;
        m_Tx.GetWalletDB()->selectCoins2(val, aid, vSelStd, vSelShielded, nShieldedMax, true);

        for (const auto& c : vSelStd)
        {
            m_Coins.m_Input.push_back(c.m_ID);
            m_Balance.Add(c.m_ID, false);
        }

        for (const auto& c : vSelShielded)
        {
            Cast::Down<ShieldedTxo::ID>(m_Coins.m_InputShielded.emplace_back()) = c.m_CoinID;
            m_Coins.m_InputShielded.back().m_Fee = feeShielded;
            m_Balance.Add(m_Coins.m_InputShielded.back());
        }

        if (!x.IsEnoughNetTx(val))
        {
            LOG_ERROR() << m_Tx.GetTxID() << "[" << m_SubTxID << "]" << " You only have " << PrintableAmount(x.m_In, false, kAmountASSET, kAmountAGROTH);
            throw TransactionFailedException(!m_Tx.IsInitiator(), TxFailureReason::NoInputs);
        }

        for (auto& cid : m_Coins.m_Input)
        {
            Coin coin;
            coin.m_ID = cid;
            if (m_Tx.GetWalletDB()->findCoin(coin))
            {
                coin.m_spentTxId = m_Tx.GetTxID();
                m_Tx.GetWalletDB()->saveCoin(coin);
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
        :public KeyKeeperHandler
        ,public std::enable_shared_from_this<HandlerInOuts>
    {
        using KeyKeeperHandler::KeyKeeperHandler;

        virtual ~HandlerInOuts() {} // auto

        struct Outputs
        {
            std::vector<IPrivateKeyKeeper2::Method::CreateOutput> m_vMethods;
            std::vector<Output::Ptr> m_Done;

            bool IsAllDone() const { return m_vMethods.size() == m_Done.size(); }

            bool OnNext()
            {
                size_t iDone = m_Done.size();
                assert(m_vMethods[iDone].m_pResult);
                m_Done.push_back(std::move(m_vMethods[iDone].m_pResult));
                return true;
            }

        } m_Outputs;

        struct Inputs
        {
            struct CoinPars :public IPrivateKeyKeeper2::Method::get_Kdf {
                CoinID m_Cid;
            };

            std::vector<CoinPars> m_vMethods;
            std::vector<Input::Ptr> m_Done;

            bool IsAllDone() const { return m_vMethods.size() == m_Done.size(); }

            bool OnNext()
            {
                size_t iDone = m_Done.size();
                CoinPars& c = m_vMethods[iDone];

                if (!c.m_pPKdf)
                    return false;

                Point::Native comm;
                CoinID::Worker(c.m_Cid).Recover(comm, *c.m_pPKdf);

                m_Done.emplace_back();
                m_Done.back().reset(new Input);
                m_Done.back()->m_Commitment = comm;

                return true;
            }

        } m_Inputs;

        struct InputsShielded
        {
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

            std::vector<TxKernelShieldedInput::Ptr> m_Done;

            TxoID m_Wnd0;
            uint32_t m_N;
            uint32_t m_Count;

            bool IsAllDone(BaseTxBuilder& b) const { return b.m_Coins.m_InputShielded.size() == m_Done.size(); }

            bool OnNext(BaseTxBuilder& b)
            {
                m_Done.push_back(std::move(m_Method.m_pKernel));
                return MoveNextSafe(b);
            }

            bool MoveNextSafe(BaseTxBuilder&);
            bool OnList(BaseTxBuilder&, proto::ShieldedList& msg);

            IMPLEMENT_GET_PARENT_OBJ(HandlerInOuts, m_InputsShielded)

        } m_InputsShielded;


        void CheckAllDone(BaseTxBuilder& b)
        {
            if (m_Outputs.IsAllDone() && m_Inputs.IsAllDone() && m_InputsShielded.IsAllDone(b))
            {
                MoveIntoVec(b.m_pTransaction->m_vOutputs, m_Outputs.m_Done);
                MoveIntoVec(b.m_pTransaction->m_vInputs, m_Inputs.m_Done);
                MoveIntoVec1(b.m_pTransaction->m_vKernels, m_InputsShielded.m_Done);

                b.m_Tx.SetParameter(TxParameterID::Inputs, b.m_pTransaction->m_vInputs);
                b.m_Tx.SetParameter(TxParameterID::InputCoinsShielded, b.m_pTransaction->m_vKernels);
                b.m_Tx.SetParameter(TxParameterID::Outputs, b.m_pTransaction->m_vOutputs);

                OnAllDone(b);
            }
        }

        bool OnNext(BaseTxBuilder& b)
        {
            if (!m_Outputs.IsAllDone())
                return m_Outputs.OnNext();

            if (!m_Inputs.IsAllDone())
                return m_Inputs.OnNext();

            assert(!m_InputsShielded.IsAllDone(b));
            return m_InputsShielded.OnNext(b);
        }

        virtual void OnSuccess(BaseTxBuilder& b) override
        {
            if (OnNext(b))
                CheckAllDone(b);
            else
                OnFailed(b, IPrivateKeyKeeper2::Status::Unspecified); // although shouldn't happen
        }
    };

    void BaseTxBuilder::GenerateInOuts()
    {
        if (Stage::None != m_GeneratingInOuts)
            return;

        KeyKeeperHandler::Ptr pHandler = std::make_shared<HandlerInOuts>(*this, m_GeneratingInOuts);
        HandlerInOuts& x = Cast::Up<HandlerInOuts>(*pHandler);

        // outputs
        x.m_Outputs.m_vMethods.resize(m_Coins.m_Output.size());
        x.m_Outputs.m_Done.reserve(m_Coins.m_Output.size());
        for (size_t i = 0; i < m_Coins.m_Output.size(); i++)
        {
            x.m_Outputs.m_vMethods[i].m_hScheme = m_Height.m_Min;
            x.m_Outputs.m_vMethods[i].m_Cid = m_Coins.m_Output[i];

            m_Tx.get_KeyKeeperStrict()->InvokeAsync(x.m_Outputs.m_vMethods[i], pHandler);
        }

        // inputs
        x.m_Inputs.m_vMethods.resize(m_Coins.m_Input.size());
        x.m_Inputs.m_Done.reserve(m_Coins.m_Input.size());
        for (size_t i = 0; i < m_Coins.m_Input.size(); i++)
        {
            HandlerInOuts::Inputs::CoinPars& c = x.m_Inputs.m_vMethods[i];
            c.m_Cid = m_Coins.m_Input[i];
            c.m_Root = !c.m_Cid.get_ChildKdfIndex(c.m_iChild);
            m_Tx.get_KeyKeeperStrict()->InvokeAsync(x.m_Inputs.m_vMethods[i], pHandler);
        }

        // inputs shielded
        x.m_InputsShielded.m_Done.reserve(m_Coins.m_InputShielded.size());
        if (!x.m_InputsShielded.MoveNextSafe(*this))
            throw TransactionFailedException(true, TxFailureReason::Unknown);

        x.CheckAllDone(*this);
    }



    bool BaseTxBuilder::HandlerInOuts::InputsShielded::MoveNextSafe(BaseTxBuilder& b)
    {
        if (IsAllDone(b))
            return true;

        // currently process inputs 1-by-1
        // don't cache retrieved elements (i.e. ignore overlaps for multiple inputs)
        const IPrivateKeyKeeper2::ShieldedInput& si = b.m_Coins.m_InputShielded[m_Done.size()];
        auto pCoin = b.m_Tx.GetWalletDB()->getShieldedCoin(si.m_Key);
        if (!pCoin)
            return false;
        const ShieldedCoin& c = *pCoin;

        Cast::Down<ShieldedTxo::ID>(m_Method) = c.m_CoinID;

        m_Method.m_pKernel = std::make_unique<TxKernelShieldedInput>();
        m_Method.m_pKernel->m_Fee = si.m_Fee;

        bool bWndLost = c.IsLargeSpendWindowLost();
        m_Method.m_pKernel->m_SpendProof.m_Cfg = bWndLost ?
            Rules::get().Shielded.m_ProofMin :
            Rules::get().Shielded.m_ProofMax;

        m_N = m_Method.m_pKernel->m_SpendProof.m_Cfg.get_N();
        if (!m_N)
            return false;

        TxoID nShieldedCurrently = 0;
        storage::getVar(*b.m_Tx.GetWalletDB(), kStateSummaryShieldedOutsDBPath, nShieldedCurrently);

        std::setmax(nShieldedCurrently, c.m_TxoID + 1); // assume stored shielded count may be inaccurate, the being-spent element must be present

        m_Method.m_iIdx = c.get_WndIndex(m_N);
        m_Wnd0 = c.m_TxoID - m_Method.m_iIdx;
        m_Count = m_N;

        TxoID nWndEnd = m_Wnd0 + m_N;
        if (nWndEnd > nShieldedCurrently)
        {
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

        b.m_Tx.GetGateway().get_shielded_list(b.m_Tx.GetTxID(), m_Wnd0, m_Count,
            [pHandler = get_ParentObj().shared_from_this(), weakTx = b.m_Tx.weak_from_this()](TxoID, uint32_t, proto::ShieldedList& msg)
        {
            auto pBldr = pHandler->m_pBuilder.lock();
            if (!pBldr)
                return;
            BaseTxBuilder& b = *pBldr;

            auto pTx = weakTx.lock();
            if (!pTx)
                return;

            try {
                if (!pHandler->m_InputsShielded.OnList(b, msg))
                    b.m_Tx.OnFailed(TxFailureReason::Unknown);
            }
            catch (const TransactionFailedException& ex) {
                b.m_Tx.OnFailed(ex.GetReason(), ex.ShouldNofify());
            }
        });

        return true;
    }

    bool BaseTxBuilder::HandlerInOuts::InputsShielded::OnList(BaseTxBuilder& b, proto::ShieldedList& msg)
    {
        if (msg.m_Items.size() > m_Count)
            return false;

        uint32_t nItems = static_cast<uint32_t>(msg.m_Items.size());

        m_Lst.m_p0 = &msg.m_Items.front();
        m_Lst.m_Skip = 0;
        m_Lst.m_vec.swap(msg.m_Items);

        m_Method.m_pList = &m_Lst;

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
                return false;

            uint32_t nDelta = m_N - nItems;
            m_Lst.m_Skip = nDelta;

            m_Method.m_iIdx += nDelta;
            assert(m_Method.m_iIdx < m_N);
        }

        b.m_Tx.get_KeyKeeperStrict()->InvokeAsync(m_Method, get_ParentObj().shared_from_this());

        return true;
    }

    void BaseTxBuilder::SetInOuts(IPrivateKeyKeeper2::Method::InOuts& m)
    {
        m.m_vInputs = m_Coins.m_Input;
        m.m_vOutputs = m_Coins.m_Output;
        m.m_vInputsShielded = m_Coins.m_InputShielded;
    }

    void BaseTxBuilder::SetCommon(IPrivateKeyKeeper2::Method::TxCommon& m)
    {
        BaseTxBuilder::SetInOuts(m);
        m.m_pKernel.reset(new TxKernelStd);
        m.m_pKernel->m_Fee = m_Fee;
        m.m_pKernel->m_Height = m_Height;
    }

    bool BaseTxBuilder::VerifyTx()
    {
        TxBase::Context::Params pars;
        TxBase::Context ctx(pars);
        ctx.m_Height.m_Min = m_Height.m_Min;
        return m_pTransaction->IsValid(ctx);
    }

    void BaseTxBuilder::VerifyAssetsEnabled()
    {
        TxFailureReason res = CheckAssetsEnabled(m_Height.m_Min);
        if (TxFailureReason::Count != res)
            throw TransactionFailedException(!m_Tx.IsInitiator(), res);
    }

    void MutualTxBuilder::MakeInputsAndChanges()
    {
        Asset::ID aid = GetAssetId();
        Amount val = GetAmount();

        if (aid)
        {
            MakeInputsAndChange(val, aid);
            val = m_Fee;
        }
        else
            val += m_Fee;

        MakeInputsAndChange(val, 0);
    }

    void MutualTxBuilder::GenerateAssetCoin(Amount amount, bool change)
    {
        CoinID cid;
        cid.set_Subkey(0);
        cid.m_Value = amount;
        cid.m_AssetID = GetAssetId();
        cid.m_Type = change ? Key::Type::Change : Key::Type::Regular;

        CreateAddNewOutput(cid);
    }

    void MutualTxBuilder::GenerateBeamCoin(Amount amount, bool change)
    {
        CoinID cid;
        cid.set_Subkey(0);
        cid.m_Value = amount;
        cid.m_AssetID = 0;
        cid.m_Type = change ? Key::Type::Change : Key::Type::Regular;

        CreateAddNewOutput(cid);
    }

    void MutualTxBuilder::CreateKernel()
    {
        if (m_Kernel)
            return;
        // create kernel
        m_Kernel = make_unique<TxKernelStd>();
        m_Kernel->m_Fee = GetFee();
        m_Kernel->m_Height.m_Min = GetMinHeight();
        m_Kernel->m_Height.m_Max = GetMaxHeight();
        m_Kernel->m_Commitment = Zero;

        m_Tx.SetParameter(TxParameterID::MaxHeight, GetMaxHeight(), m_SubTxID);

        // load kernel's extra data
        Hash::Value hv;
        if (m_Tx.GetParameter(TxParameterID::PeerLockImage, hv, m_SubTxID))
        {
			m_Kernel->m_pHashLock = make_unique<TxKernelStd::HashLock>();
			m_Kernel->m_pHashLock->m_IsImage = true;
			m_Kernel->m_pHashLock->m_Value = hv;
        }

        uintBig preImage;
        if (m_Tx.GetParameter(TxParameterID::PreImage, preImage, m_SubTxID))
        {
			m_Kernel->m_pHashLock = make_unique<TxKernelStd::HashLock>();
			m_Kernel->m_pHashLock->m_Value = hv;
		}
    }

    Point::Native MutualTxBuilder::GetPublicExcess() const
    {
        return m_PublicExcess;
    }

    Point::Native MutualTxBuilder::GetPublicNonce() const
    {
        return m_PublicNonce;
    }

    Asset::ID MutualTxBuilder::GetAssetId() const
    {
        Asset::ID assetId = Asset::s_InvalidID;
        m_Tx.GetParameter(TxParameterID::AssetID, assetId);
        return assetId;
    }

    bool MutualTxBuilder::IsAssetTx() const
    {
        return GetAssetId() != Asset::s_InvalidID;
    }

    bool MutualTxBuilder::GetPeerPublicExcessAndNonce()
    {
        return m_Tx.GetParameter(TxParameterID::PeerPublicExcess, m_PeerPublicExcess, m_SubTxID)
            && m_Tx.GetParameter(TxParameterID::PeerPublicNonce, m_PeerPublicNonce, m_SubTxID);
    }

    bool MutualTxBuilder::GetPeerSignature()
    {
        if (m_Tx.GetParameter(TxParameterID::PeerSignature, m_PeerSignature, m_SubTxID))
        {
            LOG_DEBUG() << m_Tx.GetTxID() << "[" << m_SubTxID << "]" << " Received PeerSig:\t" << Scalar(m_PeerSignature);
            return true;
        }

        return false;
    }

    MutualTxBuilder::MutualTxBuilder(BaseTransaction& tx, SubTxID subTxID, const AmountList& amountList, Amount fee)
        :BaseTxBuilder(tx, subTxID)
        , m_AmountList{ amountList }
        , m_Lifetime{ kDefaultTxLifetime }
    {
        m_Fee = fee;

        if (m_AmountList.empty())
            m_Tx.GetParameter(TxParameterID::AmountList, m_AmountList, m_SubTxID);

        if (m_Fee == 0)
            m_Tx.GetParameter(TxParameterID::Fee, m_Fee, m_SubTxID);

        Height responseTime = 0;
        if (m_Tx.GetParameter(TxParameterID::PeerResponseTime, responseTime, m_SubTxID))
        {
            auto currentHeight = m_Tx.GetWalletDB()->getCurrentHeight();
            // adjust response height, if min height din not set then then it should be equal to responce time
            m_Tx.SetParameter(TxParameterID::PeerResponseHeight, responseTime + currentHeight, m_SubTxID);
        }

        m_Tx.GetParameter(TxParameterID::Lifetime, m_Lifetime, m_SubTxID);

        CheckMinimumFee();

        m_Tx.GetParameter(TxParameterID::PublicExcess, m_PublicExcess, m_SubTxID);
        m_Tx.GetParameter(TxParameterID::PublicNonce, m_PublicNonce, m_SubTxID);
    }

    bool MutualTxBuilder::GetPeerInputsAndOutputs()
    {
        // used temporary vars to avoid non-short circuit evaluation
        bool hasInputs = m_Tx.GetParameter(TxParameterID::PeerInputs, m_PeerInputs, m_SubTxID);
        bool hasOutputs = (m_Tx.GetParameter(TxParameterID::PeerOutputs, m_PeerOutputs, m_SubTxID)
            && m_Tx.GetParameter(TxParameterID::PeerOffset, m_PeerOffset, m_SubTxID));
        return hasInputs || hasOutputs;
    }

    void MutualTxBuilder::SignSender(bool initial, bool bIsConventional)
    {
        if (Stage::InProgress == m_Signing)
            return;

        if (!initial)
        {
            if (m_Tx.GetParameter(TxParameterID::PartialSignature, m_PartialSignature, m_SubTxID))
            {
                Point::Native comm = GetPublicExcess();
                comm += m_PeerPublicExcess;

                assert(m_Kernel);
                m_Kernel->m_Commitment = comm;
                m_Kernel->UpdateID();

                m_Signing = Stage::Done;
                return;
            }
        }
        else
        {
            if (m_Tx.GetParameter(TxParameterID::PublicNonce, m_PublicNonce, m_SubTxID) &&
                m_Tx.GetParameter(TxParameterID::PublicExcess, m_PublicExcess, m_SubTxID))
            {
                m_Signing = Stage::Done;
                return;
            }
        }

        m_Signing = Stage::None;

        struct MyHandler
            :public KeyKeeperHandler
        {
            using KeyKeeperHandler::KeyKeeperHandler;

            IPrivateKeyKeeper2::Method::SignSender m_Method;
            bool m_Initial;

            virtual ~MyHandler() {} // auto

            virtual void OnSuccess(BaseTxBuilder& b_) override
            {
                MutualTxBuilder& b = Cast::Up<MutualTxBuilder>(b_);
                TxKernelStd& krn = *m_Method.m_pKernel;

                if (m_Initial)
                {
                    b.m_PublicNonce.Import(krn.m_Signature.m_NoncePub);
                    b.m_PublicExcess.Import(krn.m_Commitment);

                    b.m_Tx.SetParameter(TxParameterID::PublicNonce, krn.m_Signature.m_NoncePub, b.m_SubTxID);
                    b.m_Tx.SetParameter(TxParameterID::PublicExcess, krn.m_Commitment, b.m_SubTxID);
                    b.m_Tx.SetParameter(TxParameterID::UserConfirmationToken, m_Method.m_UserAgreement, b.m_SubTxID);
                }
                else
                {
                    b.SaveAndStore(b.m_PartialSignature, TxParameterID::PartialSignature, krn.m_Signature.m_k);

                    ECC::Scalar::Native kOffs(b.m_pTransaction->m_Offset);
                    kOffs += m_Method.m_kOffset;
                    b.SaveAndStore(b.m_pTransaction->m_Offset, TxParameterID::Offset, kOffs);

                    b.StoreKernelID();

                    b.m_Tx.FreeSlotSafe(); // release it ASAP
                }

                OnAllDone(b);
            }
        };

        KeyKeeperHandler::Ptr pHandler = std::make_shared<MyHandler>(*this, m_Signing);
        MyHandler& x = Cast::Up<MyHandler>(*pHandler);
        x.m_Initial = initial;
        IPrivateKeyKeeper2::Method::SignSender& m = x.m_Method;

        SetCommon(m);

        m.m_Slot = m_Tx.GetSlotSafe(true);
        m.m_NonConventional = !bIsConventional;

        TxKernelStd& krn = *m.m_pKernel;

        if (bIsConventional)
        {
            if (m_Tx.GetParameter(TxParameterID::PeerWalletIdentity, m.m_Peer) &&
                m_Tx.GetParameter(TxParameterID::MyWalletIdentity, m.m_MyID))
            {
                // newer scheme
                m.m_MyIDKey = m_Tx.GetMandatoryParameter<WalletIDKey>(TxParameterID::MyAddressID, m_SubTxID);
            }
            else
            {
                // legacy. Will fail for trustless key keeper.
                m.m_MyIDKey = 0;

                WalletID widMy, widPeer;
                if (!m_Tx.GetParameter(TxParameterID::PeerID, widPeer) ||
                    !m_Tx.GetParameter(TxParameterID::MyID, widMy))
                {
                    throw TransactionFailedException(true, TxFailureReason::NotEnoughDataForProof);
                }

                m.m_Peer = widPeer.m_Pk;
                m.m_MyID = widMy.m_Pk;
            }
        }
        else
        {
            // probably part of lock tx. Won't pass in trustless mode
            m.m_MyIDKey = 0;
            m.m_MyID = Zero;
            m.m_Peer = Zero;
        }

        ZeroObject(m.m_PaymentProofSignature);
        ZeroObject(krn.m_Signature);
        m.m_UserAgreement = Zero;

        if (!initial)
        {
            m_Tx.GetParameter(TxParameterID::UserConfirmationToken, m.m_UserAgreement, m_SubTxID);
            if (m.m_UserAgreement == Zero)
                throw TransactionFailedException(true, TxFailureReason::FailedToGetParameter);

            Point::Native comm = GetPublicExcess();
            comm += m_PeerPublicExcess;
            krn.m_Commitment = comm;

            comm = GetPublicNonce();
            comm += m_PeerPublicNonce;
            krn.m_Signature.m_NoncePub = comm;

            m_Tx.GetParameter(TxParameterID::PaymentConfirmation, m.m_PaymentProofSignature, m_SubTxID);
        }

        m_Tx.get_KeyKeeperStrict()->InvokeAsync(x.m_Method, pHandler);
    }

    void MutualTxBuilder::SignReceiver(bool bIsConventional)
    {
        SignReceiverOrSplit(false, bIsConventional);
    }

    void MutualTxBuilder::SignSplit()
    {
        SignReceiverOrSplit(true, true);
    }

    void MutualTxBuilder::SignReceiverOrSplit(bool bFromYourself, bool bIsConventional)
    {
        if (Stage::InProgress == m_Signing)
            return;

        if (m_Tx.GetParameter(TxParameterID::PartialSignature, m_PartialSignature, m_SubTxID))
        {
            m_Signing = Stage::Done;
            return;
        }

        m_Signing = Stage::None;

        struct MyHandler
            :public KeyKeeperHandler
        {
            using KeyKeeperHandler::KeyKeeperHandler;

            IPrivateKeyKeeper2::Method::SignReceiver m_Method;

            virtual ~MyHandler() {} // auto

            virtual void OnSuccess(BaseTxBuilder& b_) override
            {
                MutualTxBuilder& b = Cast::Up<MutualTxBuilder>(b_);
                TxKernelStd& krn = *m_Method.m_pKernel;

                b.SaveAndStore(b.m_PartialSignature, TxParameterID::PartialSignature, krn.m_Signature.m_k);

                b.m_PublicNonce.Import(krn.m_Signature.m_NoncePub);
                b.m_PublicNonce -= b.m_PeerPublicNonce;
                b.m_Tx.SetParameter(TxParameterID::PublicNonce, b.m_PublicNonce, b.m_SubTxID);

                b.m_PublicExcess.Import(krn.m_Commitment);
                b.m_PublicExcess -= b.m_PeerPublicExcess;
                b.m_Tx.SetParameter(TxParameterID::PublicExcess, b.m_PublicExcess, b.m_SubTxID);

                ECC::Scalar::Native kOffs(b.m_pTransaction->m_Offset);
                kOffs += m_Method.m_kOffset;
                b.SaveAndStore(b.m_pTransaction->m_Offset, TxParameterID::Offset, kOffs);

                if (m_Method.m_MyIDKey)
                    b.m_Tx.SetParameter(TxParameterID::PaymentConfirmation, m_Method.m_PaymentProofSignature);

                b.m_Kernel = std::move(m_Method.m_pKernel);
                b.StoreKernelID();

                OnAllDone(b);
            }
        };

        KeyKeeperHandler::Ptr pHandler = std::make_shared<MyHandler>(*this, m_Signing);
        MyHandler& x = Cast::Up<MyHandler>(*pHandler);
        IPrivateKeyKeeper2::Method::SignReceiver& m = x.m_Method;

        SetCommon(m);

        TxKernelStd& krn = *m.m_pKernel;

        m.m_NonConventional = !bIsConventional;
        m.m_Peer = Zero;
        m.m_MyIDKey = 0;

        if (bFromYourself)
        {
            // for historical reasons split is treated as "receive" tx.
            // However for IPrivateKeyKeeper2 it's not the same, coz in this "receive" the user actually looses the fee.
            IPrivateKeyKeeper2::Method::TxCommon& tx = x.m_Method; // downcast
            IPrivateKeyKeeper2::Method::SignSplit& txSplit = Cast::Up<IPrivateKeyKeeper2::Method::SignSplit>(tx);
            static_assert(sizeof(tx) == sizeof(txSplit));

            m_Tx.get_KeyKeeperStrict()->InvokeAsync(txSplit, pHandler);
        }
        else
        {
            m_Tx.GetParameter(TxParameterID::PeerWalletIdentity, m.m_Peer);

            if (m.m_Peer != Zero)
                m_Tx.GetParameter(TxParameterID::MyAddressID, m.m_MyIDKey);

            krn.m_Commitment = m_PeerPublicExcess;
            krn.m_Signature.m_NoncePub = m_PeerPublicNonce;

            m_Tx.get_KeyKeeperStrict()->InvokeAsync(x.m_Method, pHandler);
        }
    }

    void MutualTxBuilder::FinalizeSignature()
    {
        assert(m_Kernel);
        // final signature
        m_Kernel->m_Signature.m_NoncePub = GetPublicNonce() + m_PeerPublicNonce;
        m_Kernel->m_Signature.m_k = m_PartialSignature + m_PeerSignature;

        m_Tx.SetParameter(TxParameterID::Kernel, m_Kernel, m_SubTxID);
    }

    bool MutualTxBuilder::LoadKernel()
    {
        if (m_Tx.GetParameter(TxParameterID::Kernel, m_Kernel, m_SubTxID))
            return true;

        return false;
    }

    bool MutualTxBuilder::HasKernelID() const
    {
        Merkle::Hash kernelID;
        return m_Tx.GetParameter(TxParameterID::KernelID, kernelID, m_SubTxID);
    }

    Transaction::Ptr MutualTxBuilder::CreateTransaction()
    {
        assert(m_Kernel);

        const auto& kernelHeight = m_Kernel->m_Height; // alias
        // Don't display in log infinite max height
        if (kernelHeight.m_Max == MaxHeight)
        {
            LOG_INFO() << m_Tx.GetTxID() << "[" << m_SubTxID << "]"
                << " Transaction created. Kernel: " << GetKernelIDString()
                << ", min height: " << kernelHeight.m_Min;
        }
        else
        {
            LOG_INFO() << m_Tx.GetTxID() << "[" << m_SubTxID << "]"
                << " Transaction created. Kernel: " << GetKernelIDString()
                << ", min height: " << kernelHeight.m_Min
                << ", max height: " << kernelHeight.m_Max;
        }

        // create transaction
        auto transaction = make_shared<Transaction>();
        transaction->m_vInputs = move(m_pTransaction->m_vInputs);
        transaction->m_vOutputs = move(m_pTransaction->m_vOutputs);
        move(m_PeerInputs.begin(), m_PeerInputs.end(), back_inserter(transaction->m_vInputs));
        move(m_PeerOutputs.begin(), m_PeerOutputs.end(), back_inserter(transaction->m_vOutputs));

        transaction->m_vKernels = std::move(m_pTransaction->m_vKernels);
        transaction->m_vKernels.push_back(std::move(m_Kernel));
        transaction->m_Offset = m_PeerOffset + m_pTransaction->m_Offset;

        transaction->Normalize();

        return transaction;
    }

    bool MutualTxBuilder::IsPeerSignatureValid() const
    {
        Signature peerSig;
        peerSig.m_NoncePub = m_PeerPublicNonce + GetPublicNonce();
        peerSig.m_k = m_PeerSignature;
        return peerSig.IsValidPartial(m_Kernel->m_Internal.m_ID, m_PeerPublicNonce, m_PeerPublicExcess);
    }

    Amount MutualTxBuilder::GetAmount() const
    {
        return std::accumulate(m_AmountList.begin(), m_AmountList.end(), 0ULL);
    }

    const AmountList& MutualTxBuilder::GetAmountList() const
    {
        return m_AmountList;
    }

    Amount MutualTxBuilder::GetFee() const
    {
        return m_Fee;
    }

    Height MutualTxBuilder::GetLifetime() const
    {
        return m_Lifetime;
    }

    Height MutualTxBuilder::GetMinHeight() const
    {
        return m_Height.m_Min;
    }

    Height MutualTxBuilder::GetMaxHeight() const
    {
        if (m_Height.m_Max == MaxHeight)
            return m_Height.m_Min + m_Lifetime;

        return m_Height.m_Max;
    }

    const Scalar::Native& MutualTxBuilder::GetPartialSignature() const
    {
        return m_PartialSignature;
    }

    const TxKernel& MutualTxBuilder::GetKernel() const
    {
        assert(m_Kernel);
        return *m_Kernel;
    }

    Hash::Value MutualTxBuilder::GetLockImage() const
    {
		if (!m_Kernel->m_pHashLock)
			return Zero;

        Hash::Value hv;
		return m_Kernel->m_pHashLock->get_Image(hv);
    }

    const Merkle::Hash& MutualTxBuilder::GetKernelID() const
    {
        if (!m_KernelID)
        {
            Merkle::Hash kernelID;
            if (m_Tx.GetParameter(TxParameterID::KernelID, kernelID, m_SubTxID))
            {
                m_KernelID = kernelID;
            }
            else
            {
                throw std::runtime_error("KernelID is not stored");
            }
        }
        return *m_KernelID;
    }

    void MutualTxBuilder::StoreKernelID()
    {
        assert(m_Kernel);
        Point::Native totalPublicExcess = GetPublicExcess();
        totalPublicExcess += m_PeerPublicExcess;
        m_Kernel->m_Commitment = totalPublicExcess;

        m_Kernel->UpdateID();
        m_Tx.SetParameter(TxParameterID::KernelID, m_Kernel->m_Internal.m_ID, m_SubTxID);
    }

    void MutualTxBuilder::ResetKernelID()
    {
        Merkle::Hash emptyHash = Zero;
        m_Tx.SetParameter(TxParameterID::KernelID, emptyHash, m_SubTxID);
        m_KernelID.reset();
    }

    string MutualTxBuilder::GetKernelIDString() const
    {
        Merkle::Hash kernelID;
        m_Tx.GetParameter(TxParameterID::KernelID, kernelID, m_SubTxID);
        char sz[Merkle::Hash::nTxtLen + 1];
        kernelID.Print(sz);
        return string(sz);
    }

    SubTxID MutualTxBuilder::GetSubTxID() const
    {
        return m_SubTxID;
    }

    bool MutualTxBuilder::UpdateMaxHeight()
    {
        //Merkle::Hash kernelId;
        //if (m_Tx.GetParameter(TxParameterID::MaxHeight, m_Height.m_Max, m_SubTxID) ||
        //    m_Tx.GetParameter(TxParameterID::KernelID, kernelId, m_SubTxID))
        //    return true;
        if (MaxHeight != m_Height.m_Max)
            return true;

        Height hPeerMax = MaxHeight;
        m_Tx.GetParameter(TxParameterID::PeerMaxHeight, hPeerMax, m_SubTxID);

        bool isInitiator = m_Tx.IsInitiator();

        bool hasPeerMaxHeight = hPeerMax < MaxHeight;

        if (!isInitiator)
        {
            if (m_Tx.GetParameter(TxParameterID::Lifetime, m_Lifetime, m_SubTxID))
            {
                Block::SystemState::Full state;
                if (m_Tx.GetTip(state))
                {
                    m_Height.m_Max = state.m_Height + m_Lifetime;
                }
            }
            else if (hasPeerMaxHeight)
            {
                m_Height.m_Max = hPeerMax;
            }
        }
        else if (hasPeerMaxHeight)
        {
            if (!IsAcceptableMaxHeight(hPeerMax))
                return false;

            m_Height.m_Max = hPeerMax;
        }

        return true;
    }

    bool MutualTxBuilder::IsAcceptableMaxHeight(Height hPeerMax) const
    {
        Height lifetime = 0;
        Height peerResponceHeight = 0;
        if (!m_Tx.GetParameter(TxParameterID::Lifetime, lifetime, m_SubTxID)
            || !m_Tx.GetParameter(TxParameterID::PeerResponseHeight, peerResponceHeight, m_SubTxID))
        {
            // possible situation during update from older version
            return true;
        }

        return hPeerMax <= lifetime + peerResponceHeight;
    }

    Amount MutualTxBuilder::GetMinimumFee() const
    {
        auto numberOfOutputs = GetAmountList().size() + 1; // +1 for possible change to simplify logic TODO: need to review

        return wallet::GetMinimumFee(numberOfOutputs);
    }

    void MutualTxBuilder::CheckMinimumFee()
    {
        // after 1st fork fee should be >= minimal fee
        if (Rules::get().pForks[1].m_Height <= GetMinHeight())
        {
            auto minimalFee = GetMinimumFee();
            Amount userFee = 0;
            if (m_Tx.GetParameter(TxParameterID::Fee, userFee, m_SubTxID))
            {
                if (userFee < minimalFee)
                {
                    stringstream ss;
                    ss << "The minimum fee must be: " << minimalFee << " .";
                    throw TransactionFailedException(false, TxFailureReason::FeeIsTooSmall, ss.str().c_str());
                }
            }
        }
    }
}
