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
#include "base_transaction.h"
#include "strings_resources.h"

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

    BaseTxBuilder::KeyKeeperHandler::KeyKeeperHandler(BaseTxBuilder& b, bool& bLink)
    {
        m_pBuilder = b.shared_from_this();

        m_pLink = &bLink;
        assert(!bLink);
        bLink = true;
    }

    BaseTxBuilder::KeyKeeperHandler::~KeyKeeperHandler()
    {
        if (m_pLink)
        {
            std::shared_ptr<BaseTxBuilder> pBld = m_pBuilder.lock();
            if (pBld)
                Detach(*pBld);
        }
    }

    void BaseTxBuilder::KeyKeeperHandler::Detach(BaseTxBuilder&)
    {
        if (m_pLink)
        {
            assert(*m_pLink);
            *m_pLink = false;
            m_pLink = nullptr;
        }
    }

    void BaseTxBuilder::KeyKeeperHandler::OnDone(IPrivateKeyKeeper2::Status::Type n)
    {
        if (m_pLink)
        {
            std::shared_ptr<BaseTxBuilder> pBld = m_pBuilder.lock();
            if (pBld)
            {
                if (IPrivateKeyKeeper2::Status::Success == n)
                {
                    ITransaction::Ptr pGuard(pBld->m_Tx.shared_from_this()); // extra ref on transaction object.
                    // Otherwise it can crash in Update() -> CompleteTx(), which will remove its ptr from live tx map
                    OnSuccess(*pBld);
                }
                else
                    OnFailed(*pBld, n);
            }
            else
                m_pLink = nullptr;
        }
    }

    void BaseTxBuilder::KeyKeeperHandler::OnFailed(BaseTxBuilder& b, IPrivateKeyKeeper2::Status::Type n)
    {
        Detach(b);
        b.m_Tx.OnFailed(BaseTransaction::KeyKeeperErrorToFailureReason(n), true);
    }

    void BaseTxBuilder::KeyKeeperHandler::OnAllDone(BaseTxBuilder& b)
    {
        Detach(b);
        b.m_Tx.Update(); // may complete transaction
    }

    BaseTxBuilder::BaseTxBuilder(BaseTransaction& tx, SubTxID subTxID, const AmountList& amountList, Amount fee)
        : m_Tx{ tx }
        , m_SubTxID(subTxID)
        , m_AssetId(0)
        , m_AmountList{ amountList }
        , m_Fee{ fee }
        , m_ChangeBeam{0}
        , m_ChangeAsset{0}
        , m_Lifetime{ kDefaultTxLifetime }
        , m_MinHeight{ 0 }
        , m_MaxHeight{ MaxHeight }
        , m_PeerMaxHeight{ MaxHeight }
    {
        if (m_AmountList.empty())
        {
            m_Tx.GetParameter(TxParameterID::AmountList, m_AmountList, m_SubTxID);
        }
        if (m_Fee == 0)
        {
            m_Tx.GetParameter(TxParameterID::Fee, m_Fee, m_SubTxID);
        }
        m_Tx.GetParameter(TxParameterID::AssetID, m_AssetId, m_SubTxID);
    }

    void BaseTxBuilder::SelectInputs()
    {
        CoinIDList preselectedCoinIDs;
        vector<Coin> coins;

        Amount preselAmountBeam = 0;
        Amount preselAmountAsset = 0;

        if (m_Tx.GetParameter(TxParameterID::PreselectedCoins, preselectedCoinIDs, m_SubTxID) && !preselectedCoinIDs.empty())
        {
            coins = m_Tx.GetWalletDB()->getCoinsByID(preselectedCoinIDs);
            for (auto& coin : coins)
            {
                if (!coin.isAsset())
                    preselAmountBeam += coin.getAmount();
                else
                {
                    if (coin.isAsset(m_AssetId))
                        preselAmountAsset += coin.getAmount();
                }
                coin.m_spentTxId = m_Tx.GetTxID();
            }
            m_Tx.GetWalletDB()->saveCoins(coins);
        }

        const bool isBeamTransaction = m_AssetId == 0;
        const Amount amountBeamWithFee = (isBeamTransaction ? GetAmount() : 0) + GetFee();
        if (preselAmountBeam < amountBeamWithFee)
        {
            auto selectedCoins = m_Tx.GetWalletDB()->selectCoins(amountBeamWithFee - preselAmountBeam, Zero);
            copy(selectedCoins.begin(), selectedCoins.end(), back_inserter(coins));
            if (selectedCoins.empty())
            {
                storage::Totals allTotals(*m_Tx.GetWalletDB());
                const auto& totals = allTotals.GetTotals(Zero);
                LOG_ERROR() << m_Tx.GetTxID() << "[" << m_SubTxID << "]" << " You only have " << PrintableAmount(totals.Avail);
                throw TransactionFailedException(!m_Tx.IsInitiator(), TxFailureReason::NoInputs);
            }
        }

        const Amount amountAsset = isBeamTransaction ? 0 : GetAmount();
        if (preselAmountAsset < amountAsset)
        {
            auto selectedCoins = m_Tx.GetWalletDB()->selectCoins(amountAsset - preselAmountAsset, m_AssetId);
            if (selectedCoins.empty())
            {
                storage::Totals allTotals(*m_Tx.GetWalletDB());
                const auto& totals = allTotals.GetTotals(m_AssetId);
                LOG_ERROR() << m_Tx.GetTxID() << "[" << m_SubTxID << "]" << " You only have " << PrintableAmount(totals.Avail, false, kAmountASSET, kAmountAGROTH);
                throw TransactionFailedException(!m_Tx.IsInitiator(), TxFailureReason::NoInputs);
            }
            copy(selectedCoins.begin(), selectedCoins.end(), back_inserter(coins));
        }

        m_InputCoins.reserve(coins.size());
        Amount totalBeam = 0;
        Amount totalAsset = 0;
        for (auto& coin : coins)
        {
            coin.m_spentTxId = m_Tx.GetTxID();
            if (coin.isAsset()) totalAsset += coin.m_ID.m_Value;
            else totalBeam += coin.m_ID.m_Value;
            m_InputCoins.push_back(coin.m_ID);
        }

        m_ChangeBeam  = totalBeam  - amountBeamWithFee;
        m_ChangeAsset = totalAsset - amountAsset;

        m_Tx.SetParameter(TxParameterID::ChangeBeam, m_ChangeBeam, false, m_SubTxID);
        m_Tx.SetParameter(TxParameterID::ChangeAsset, m_ChangeAsset, false, m_SubTxID);
        m_Tx.SetParameter(TxParameterID::InputCoins, m_InputCoins, false, m_SubTxID);

        m_Tx.GetWalletDB()->saveCoins(coins);
    }

    void BaseTxBuilder::AddChange()
    {
        if (m_ChangeBeam)
        {
            GenerateBeamCoin(m_ChangeBeam, true);
        }

        if (m_ChangeAsset)
        {
            GenerateAssetCoin(m_ChangeAsset, true);
        }
    }

    void BaseTxBuilder::GenerateAssetCoin(Amount amount, bool change)
    {
        Coin newUtxo = m_Tx.GetWalletDB()->generateNewCoin(amount, m_AssetId);
        if (change)
        {
            newUtxo.m_ID.m_Type = Key::Type::Change;
        }
        newUtxo.m_createTxId = m_Tx.GetTxID();
        m_Tx.GetWalletDB()->storeCoin(newUtxo);
        m_OutputCoins.push_back(newUtxo.m_ID);
        m_Tx.SetParameter(TxParameterID::OutputCoins, m_OutputCoins, false, m_SubTxID);
    }

    void BaseTxBuilder::GenerateBeamCoin(Amount amount, bool change)
    {
        Coin newUtxo = m_Tx.GetWalletDB()->generateNewCoin(amount, Zero);
        if (change)
        {
            newUtxo.m_ID.m_Type = Key::Type::Change;
        }
        newUtxo.m_createTxId = m_Tx.GetTxID();
        m_Tx.GetWalletDB()->storeCoin(newUtxo);
        m_OutputCoins.push_back(newUtxo.m_ID);
        m_Tx.SetParameter(TxParameterID::OutputCoins, m_OutputCoins, false, m_SubTxID);
    }

    bool BaseTxBuilder::CreateOutputs()
    {
        if (GetOutputs() || m_OutputCoins.empty())
        {
            return false;
        }

        if (m_CreatingOutputs)
            return true; // already in progress

        struct MyHandler
            :public KeyKeeperHandler
        {
            using KeyKeeperHandler::KeyKeeperHandler;

            std::vector<IPrivateKeyKeeper2::Method::CreateOutput> m_vCalls;
            std::vector<Output::Ptr> m_Outputs;

            virtual ~MyHandler() {} // auto

            virtual void OnSuccess(BaseTxBuilder& b) override
            {
                size_t iDone = m_Outputs.size();
                assert(iDone < m_vCalls.size());
                assert(m_vCalls[iDone].m_pResult);

                m_Outputs.push_back(std::move(m_vCalls[iDone].m_pResult));

                if (m_Outputs.size() == m_vCalls.size())
                {
                    // all done
                    b.m_Outputs = std::move(m_Outputs);
                    b.FinalizeOutputs();
                    OnAllDone(b);
                }
            }
        };

        KeyKeeperHandler::Ptr pHandler = std::make_shared<MyHandler>(*this, m_CreatingOutputs);
        MyHandler& x = Cast::Up<MyHandler>(*pHandler);

        x.m_vCalls.resize(m_OutputCoins.size());
        x.m_Outputs.reserve(m_OutputCoins.size());
        for (size_t i = 0; i < m_OutputCoins.size(); i++)
        {
            x.m_vCalls[i].m_hScheme = m_MinHeight;
            x.m_vCalls[i].m_Cid = m_OutputCoins[i];

            m_Tx.get_KeyKeeperStrict()->InvokeAsync(x.m_vCalls[i], pHandler);
        }

        return true;// true if async
    }

    bool BaseTxBuilder::FinalizeOutputs()
    {
        m_Tx.SetParameter(TxParameterID::Outputs, m_Outputs, false, m_SubTxID);

        // TODO: check transaction size here

        return true;
    }

    bool BaseTxBuilder::CreateInputs()
    {
        if (GetInputs() || GetInputCoins().empty())
        {
            return false;
        }

        if (m_CreatingInputs)
            return true; // already in progress

        struct MyHandler
            :public KeyKeeperHandler
        {
            using KeyKeeperHandler::KeyKeeperHandler;

            struct CoinPars :public IPrivateKeyKeeper2::Method::get_Kdf {
                CoinID m_Cid;
            };

            std::vector<CoinPars> m_vCalls;
            std::vector<Input::Ptr> m_Inputs;

            virtual ~MyHandler() {} // auto

            virtual void OnSuccess(BaseTxBuilder& b) override
            {
                size_t iDone = m_Inputs.size();
                assert(iDone < m_vCalls.size());
                CoinPars& c = m_vCalls[iDone];

                if (!c.m_pPKdf)
                {
                    OnFailed(b, IPrivateKeyKeeper2::Status::Unspecified); // although shouldn't happen
                    return;
                }

                Point::Native comm;
                CoinID::Worker(c.m_Cid).Recover(comm, *c.m_pPKdf);

                m_Inputs.emplace_back();
                m_Inputs.back().reset(new Input);
                m_Inputs.back()->m_Commitment = comm;

                if (m_Inputs.size() == m_vCalls.size())
                {
                    // all done
                    b.m_Inputs = std::move(m_Inputs);
                    b.FinalizeInputs();
                    OnAllDone(b);
                }
            }
        };

        KeyKeeperHandler::Ptr pHandler = std::make_shared<MyHandler>(*this, m_CreatingInputs);
        MyHandler& x = Cast::Up<MyHandler>(*pHandler);

        x.m_vCalls.resize(m_InputCoins.size());
        x.m_Inputs.reserve(m_InputCoins.size());
        for (size_t i = 0; i < m_InputCoins.size(); i++)
        {
            MyHandler::CoinPars& c = x.m_vCalls[i];
            c.m_Cid = m_InputCoins[i];
            c.m_Root = !c.m_Cid.get_ChildKdfIndex(c.m_iChild);
            m_Tx.get_KeyKeeperStrict()->InvokeAsync(x.m_vCalls[i], pHandler);
        }

        return true;// true if async
    }

    void BaseTxBuilder::FinalizeInputs()
    {
        m_Tx.SetParameter(TxParameterID::Inputs, m_Inputs, false, m_SubTxID);
    }

    void BaseTxBuilder::CreateKernel()
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

    void BaseTxBuilder::GenerateNonce()
    {
        // Don't store the generated nonce for the kernel multisig. Instead - store the raw random, from which the nonce is derived using kdf.
        if (!m_Tx.GetParameter(TxParameterID::NonceSlot, m_NonceSlot, m_SubTxID))
        {
            m_NonceSlot = m_Tx.GetKeyKeeper()->AllocateNonceSlotSync();
            m_Tx.SetParameter(TxParameterID::NonceSlot, m_NonceSlot, false, m_SubTxID);
        }
        
        if (!m_Tx.GetParameter(TxParameterID::PublicNonce, m_PublicNonce, m_SubTxID))
        {
            auto pt = m_Tx.GetKeyKeeper()->GenerateNonceSync(m_NonceSlot);
            m_PublicNonce.Import(pt);
            m_Tx.SetParameter(TxParameterID::PublicNonce, m_PublicNonce, false, m_SubTxID);
        }
    }

    Point::Native BaseTxBuilder::GetPublicExcess() const
    {
        return m_PublicExcess;
    }

    Point::Native BaseTxBuilder::GetPublicNonce() const
    {
        return m_PublicNonce;
    }

    Asset::ID BaseTxBuilder::GetAssetId() const
    {
        return m_AssetId;
    }

    bool BaseTxBuilder::GetPeerPublicExcessAndNonce()
    {
        return m_Tx.GetParameter(TxParameterID::PeerPublicExcess, m_PeerPublicExcess, m_SubTxID)
            && m_Tx.GetParameter(TxParameterID::PeerPublicNonce, m_PeerPublicNonce, m_SubTxID);
    }

    bool BaseTxBuilder::GetPeerSignature()
    {
        if (m_Tx.GetParameter(TxParameterID::PeerSignature, m_PeerSignature, m_SubTxID))
        {
            LOG_DEBUG() << m_Tx.GetTxID() << "[" << m_SubTxID << "]" << " Received PeerSig:\t" << Scalar(m_PeerSignature);
            return true;
        }

        return false;
    }

    bool BaseTxBuilder::GetInitialTxParams()
    {
        m_Tx.GetParameter(TxParameterID::Inputs, m_Inputs, m_SubTxID);
        m_Tx.GetParameter(TxParameterID::Outputs, m_Outputs, m_SubTxID);
        bool hasInputs = m_Tx.GetParameter(TxParameterID::InputCoins, m_InputCoins, m_SubTxID);
        bool hasOutputs = m_Tx.GetParameter(TxParameterID::OutputCoins, m_OutputCoins, m_SubTxID);
        m_Tx.GetParameter(TxParameterID::AssetID, m_AssetId, m_SubTxID);

        if (!m_Tx.GetParameter(TxParameterID::MinHeight, m_MinHeight, m_SubTxID))
        {
            // adjust min height, this allows create transaction when node is out of sync
            auto currentHeight = m_Tx.GetWalletDB()->getCurrentHeight();
            m_MinHeight = currentHeight;
            m_Tx.SetParameter(TxParameterID::MinHeight, m_MinHeight, m_SubTxID);

            Height responseTime = 0;
            if (m_Tx.GetParameter(TxParameterID::PeerResponseTime, responseTime, m_SubTxID))
            {
                // adjust response height, if min height din not set then then it should be equal to responce time
                m_Tx.SetParameter(TxParameterID::PeerResponseHeight, responseTime + currentHeight, m_SubTxID);
            }

        }
        m_Tx.GetParameter(TxParameterID::Lifetime, m_Lifetime, m_SubTxID);
        m_Tx.GetParameter(TxParameterID::PeerMaxHeight, m_PeerMaxHeight, m_SubTxID);

        CheckMinimumFee();

        m_Tx.GetParameter(TxParameterID::Offset, m_Offset, m_SubTxID);
        m_Tx.GetParameter(TxParameterID::PublicExcess, m_PublicExcess, m_SubTxID);
        m_Tx.GetParameter(TxParameterID::PublicNonce, m_PublicNonce, m_SubTxID);

        return hasInputs || hasOutputs; 
    }

    bool BaseTxBuilder::GetInputs()
    {
        return m_Tx.GetParameter(TxParameterID::Inputs, m_Inputs, m_SubTxID);
    }

    bool BaseTxBuilder::GetOutputs()
    {
        return m_Tx.GetParameter(TxParameterID::Outputs, m_Outputs, m_SubTxID);
    }

    bool BaseTxBuilder::GetPeerInputsAndOutputs()
    {
        // used temporary vars to avoid non-short circuit evaluation
        bool hasInputs = m_Tx.GetParameter(TxParameterID::PeerInputs, m_PeerInputs, m_SubTxID);
        bool hasOutputs = (m_Tx.GetParameter(TxParameterID::PeerOutputs, m_PeerOutputs, m_SubTxID)
            && m_Tx.GetParameter(TxParameterID::PeerOffset, m_PeerOffset, m_SubTxID));
        return hasInputs || hasOutputs;
    }

    bool BaseTxBuilder::SignSender(bool initial)
    {
        if (m_Signing)
            return true;

        if (!initial)
        {
            if (m_Tx.GetParameter(TxParameterID::PartialSignature, m_PartialSignature, m_SubTxID))
            {
                Point::Native comm = GetPublicExcess();
                comm += m_PeerPublicExcess;

                assert(m_Kernel);
                m_Kernel->m_Commitment = comm;
                m_Kernel->UpdateID();

                return false;
            }
        }
        else
        {
            if (m_Tx.GetParameter(TxParameterID::PublicNonce, m_PublicNonce, m_SubTxID) &&
                m_Tx.GetParameter(TxParameterID::PublicExcess, m_PublicExcess, m_SubTxID))
            {
                return false;
            }
        }

        struct MyHandler
            :public KeyKeeperHandler
        {
            using KeyKeeperHandler::KeyKeeperHandler;

            IPrivateKeyKeeper2::Method::SignSender m_Method;
            bool m_Initial;

            virtual ~MyHandler() {} // auto

            virtual void OnSuccess(BaseTxBuilder& b) override
            {
                TxKernelStd& krn = *m_Method.m_pKernel;

                if (m_Initial)
                {
                    b.StoreAndLoad(TxParameterID::PublicNonce, krn.m_Signature.m_NoncePub, b.m_PublicNonce);
                    b.StoreAndLoad(TxParameterID::PublicExcess, krn.m_Commitment, b.m_PublicExcess);

                    b.m_Tx.SetParameter(TxParameterID::UserConfirmationToken, m_Method.m_UserAgreement, b.m_SubTxID);
                }
                else
                {
                    b.StoreAndLoad(TxParameterID::PartialSignature, krn.m_Signature.m_k, b.m_PartialSignature);
                    b.m_Offset = m_Method.m_kOffset;
                    b.m_Tx.SetParameter(TxParameterID::Offset, b.m_Offset, b.m_SubTxID);
                    b.StoreKernelID();
                }

                OnAllDone(b);
            }
        };

        KeyKeeperHandler::Ptr pHandler = std::make_shared<MyHandler>(*this, m_Signing);
        MyHandler& x = Cast::Up<MyHandler>(*pHandler);
        x.m_Initial = initial;
        IPrivateKeyKeeper2::Method::SignSender& m = x.m_Method;

        m.m_vInputs = m_InputCoins;
        m.m_vOutputs = m_OutputCoins;
        m.m_pKernel.reset(new TxKernelStd);
        m.m_nonceSlot = static_cast<uint32_t>(m_NonceSlot); // TODO

        TxKernelStd& krn = *m.m_pKernel;
        krn.m_Fee = m_Fee;
        krn.m_Height = { GetMinHeight(), GetMaxHeight() };

        if (m_Tx.GetParameter(TxParameterID::PeerSecureWalletID, m.m_Peer) &&
            m_Tx.GetParameter(TxParameterID::MySecureWalletID, m.m_MyID))
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

        return true;
    }

    bool BaseTxBuilder::SignReceiver()
    {
        return SignReceiver(false);
    }

    bool BaseTxBuilder::SignSplit()
    {
        return SignReceiver(true);
    }

    bool BaseTxBuilder::SignReceiver(bool bFromYourself)
    {
        if (m_Tx.GetParameter(TxParameterID::PartialSignature, m_PartialSignature, m_SubTxID))
            return false;

        if (m_Signing)
            return true;

        struct MyHandler
            :public KeyKeeperHandler
        {
            using KeyKeeperHandler::KeyKeeperHandler;

            IPrivateKeyKeeper2::Method::SignReceiver m_Method;

            virtual ~MyHandler() {} // auto

            virtual void OnSuccess(BaseTxBuilder& b) override
            {
                TxKernelStd& krn = *m_Method.m_pKernel;

                b.StoreAndLoad(TxParameterID::PartialSignature, krn.m_Signature.m_k, b.m_PartialSignature);

                b.m_PublicNonce.Import(krn.m_Signature.m_NoncePub);
                b.m_PublicNonce -= b.m_PeerPublicNonce;
                b.m_Tx.SetParameter(TxParameterID::PublicNonce, b.m_PublicNonce, b.m_SubTxID);

                b.m_PublicExcess.Import(krn.m_Commitment);
                b.m_PublicExcess -= b.m_PeerPublicExcess;
                b.m_Tx.SetParameter(TxParameterID::PublicExcess, b.m_PublicExcess, b.m_SubTxID);

                b.m_Offset = m_Method.m_kOffset;
                b.m_Tx.SetParameter(TxParameterID::Offset, b.m_Offset, b.m_SubTxID);

                if (m_Method.m_MyIDKey)
                    b.m_Tx.SetParameter(TxParameterID::PaymentConfirmation, m_Method.m_PaymentProofSignature);

                b.StoreKernelID();

                OnAllDone(b);
            }
        };

        KeyKeeperHandler::Ptr pHandler = std::make_shared<MyHandler>(*this, m_Signing);
        MyHandler& x = Cast::Up<MyHandler>(*pHandler);
        IPrivateKeyKeeper2::Method::SignReceiver& m = x.m_Method;

        m.m_vInputs = m_InputCoins;
        m.m_vOutputs = m_OutputCoins;
        m.m_pKernel.reset(new TxKernelStd);

        TxKernelStd& krn = *m.m_pKernel;
        krn.m_Fee = m_Fee;
        krn.m_Height = { GetMinHeight(), GetMaxHeight() };

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
            m_Tx.GetParameter(TxParameterID::PeerSecureWalletID, m.m_Peer);

            if (m.m_Peer != Zero)
                m_Tx.GetParameter(TxParameterID::MyAddressID, m.m_MyIDKey);

            krn.m_Commitment = m_PeerPublicExcess;
            krn.m_Signature.m_NoncePub = m_PeerPublicNonce;

            m_Tx.get_KeyKeeperStrict()->InvokeAsync(x.m_Method, pHandler);
        }

        return true;
    }

    void BaseTxBuilder::FinalizeSignature()
    {
        assert(m_Kernel);
        // final signature
        m_Kernel->m_Signature.m_NoncePub = GetPublicNonce() + m_PeerPublicNonce;
        m_Kernel->m_Signature.m_k = m_PartialSignature + m_PeerSignature;

        m_Tx.SetParameter(TxParameterID::Kernel, m_Kernel, m_SubTxID);
    }

    bool BaseTxBuilder::LoadKernel()
    {
        if (m_Tx.GetParameter(TxParameterID::Kernel, m_Kernel, m_SubTxID))
        {
            GetInitialTxParams();
            return true;
        }
        return false;
    }

    bool BaseTxBuilder::HasKernelID() const
    {
        Merkle::Hash kernelID;
        return m_Tx.GetParameter(TxParameterID::KernelID, kernelID, m_SubTxID);
    }

    Transaction::Ptr BaseTxBuilder::CreateTransaction()
    {
        assert(m_Kernel);
        // Don't display in log infinite max height
        if (m_Kernel->m_Height.m_Max == MaxHeight)
        {
            LOG_INFO() << m_Tx.GetTxID() << "[" << m_SubTxID << "]"
                << " Transaction created. Kernel: " << GetKernelIDString()
                << ", min height: " << m_Kernel->m_Height.m_Min;
        }
        else
        {
            LOG_INFO() << m_Tx.GetTxID() << "[" << m_SubTxID << "]"
                << " Transaction created. Kernel: " << GetKernelIDString()
                << ", min height: " << m_Kernel->m_Height.m_Min
                << ", max height: " << m_Kernel->m_Height.m_Max;
        }

        // create transaction
        auto transaction = make_shared<Transaction>();
        transaction->m_vKernels.push_back(move(m_Kernel));
        transaction->m_Offset = m_Offset + m_PeerOffset;
        transaction->m_vInputs = move(m_Inputs);
        transaction->m_vOutputs = move(m_Outputs);
        move(m_PeerInputs.begin(), m_PeerInputs.end(), back_inserter(transaction->m_vInputs));
        move(m_PeerOutputs.begin(), m_PeerOutputs.end(), back_inserter(transaction->m_vOutputs));

        transaction->Normalize();

        return transaction;
    }

    bool BaseTxBuilder::IsPeerSignatureValid() const
    {
        Signature peerSig;
        peerSig.m_NoncePub = m_PeerPublicNonce + GetPublicNonce();
        peerSig.m_k = m_PeerSignature;
        return peerSig.IsValidPartial(m_Kernel->m_Internal.m_ID, m_PeerPublicNonce, m_PeerPublicExcess);
    }

    Amount BaseTxBuilder::GetAmount() const
    {
        return std::accumulate(m_AmountList.begin(), m_AmountList.end(), 0ULL);
    }

    const AmountList& BaseTxBuilder::GetAmountList() const
    {
        return m_AmountList;
    }

    Amount BaseTxBuilder::GetFee() const
    {
        return m_Fee;
    }

    Height BaseTxBuilder::GetLifetime() const
    {
        return m_Lifetime;
    }

    Height BaseTxBuilder::GetMinHeight() const
    {
        return m_MinHeight;
    }

    Height BaseTxBuilder::GetMaxHeight() const
    {
        if (m_MaxHeight == MaxHeight)
        {
            return m_MinHeight + m_Lifetime;
        }
        return m_MaxHeight;
    }

    const vector<Input::Ptr>& BaseTxBuilder::GetInputs() const
    {
        return m_Inputs;
    }

    const vector<Output::Ptr>& BaseTxBuilder::GetOutputs() const
    {
        return m_Outputs;
    }

    const Scalar::Native& BaseTxBuilder::GetOffset() const
    {
        return m_Offset;
    }

    const Scalar::Native& BaseTxBuilder::GetPartialSignature() const
    {
        return m_PartialSignature;
    }

    const TxKernel& BaseTxBuilder::GetKernel() const
    {
        assert(m_Kernel);
        return *m_Kernel;
    }

    Hash::Value BaseTxBuilder::GetLockImage() const
    {
		if (!m_Kernel->m_pHashLock)
			return Zero;

        Hash::Value hv;
		return m_Kernel->m_pHashLock->get_Image(hv);
    }

    const Merkle::Hash& BaseTxBuilder::GetKernelID() const
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

    void BaseTxBuilder::StoreKernelID()
    {
        assert(m_Kernel);
        Point::Native totalPublicExcess = GetPublicExcess();
        totalPublicExcess += m_PeerPublicExcess;
        m_Kernel->m_Commitment = totalPublicExcess;

        m_Kernel->UpdateID();
        m_Tx.SetParameter(TxParameterID::KernelID, m_Kernel->m_Internal.m_ID, m_SubTxID);
    }

    string BaseTxBuilder::GetKernelIDString() const
    {
        Merkle::Hash kernelID;
        m_Tx.GetParameter(TxParameterID::KernelID, kernelID, m_SubTxID);
        char sz[Merkle::Hash::nTxtLen + 1];
        kernelID.Print(sz);
        return string(sz);
    }

    SubTxID BaseTxBuilder::GetSubTxID() const
    {
        return m_SubTxID;
    }

    bool BaseTxBuilder::UpdateMaxHeight()
    {
        Merkle::Hash kernelId;
        if (!m_Tx.GetParameter(TxParameterID::MaxHeight, m_MaxHeight, m_SubTxID) &&
            !m_Tx.GetParameter(TxParameterID::KernelID, kernelId, m_SubTxID))
        {
            bool isInitiator = m_Tx.IsInitiator();
            bool hasPeerMaxHeight = m_PeerMaxHeight < MaxHeight;
            if (!isInitiator)
            {
                if (m_Tx.GetParameter(TxParameterID::Lifetime, m_Lifetime, m_SubTxID))
                {
                    Block::SystemState::Full state;
                    if (m_Tx.GetTip(state))
                    {
                        m_MaxHeight = state.m_Height + m_Lifetime;
                    }
                }
                else if (hasPeerMaxHeight)
                {
                    m_MaxHeight = m_PeerMaxHeight;
                }
            }
            else if (hasPeerMaxHeight)
            {
                if (IsAcceptableMaxHeight())
                {
                    m_MaxHeight = m_PeerMaxHeight;
                }
                else
                {
                    return false;
                }
            }
        }
        return true;
    }

    bool BaseTxBuilder::IsAcceptableMaxHeight() const
    {
        Height lifetime = 0;
        Height peerResponceHeight = 0;
        if (!m_Tx.GetParameter(TxParameterID::Lifetime, lifetime, m_SubTxID)
            || !m_Tx.GetParameter(TxParameterID::PeerResponseHeight, peerResponceHeight, m_SubTxID))
        {
            // possible situation during update from older version
            return true;
        }
        Height maxAcceptableHeight = lifetime + peerResponceHeight;
        return m_PeerMaxHeight < MaxHeight&& m_PeerMaxHeight <= maxAcceptableHeight;
    }

    const std::vector<Coin::ID>& BaseTxBuilder::GetInputCoins() const
    {
        return m_InputCoins;
    }

    const std::vector<Coin::ID>& BaseTxBuilder::GetOutputCoins() const
    {
        return m_OutputCoins;
    }

    Amount BaseTxBuilder::GetMinimumFee() const
    {
        auto numberOfOutputs = GetAmountList().size() + 1; // +1 for possible change to simplify logic TODO: need to review

        return wallet::GetMinimumFee(numberOfOutputs);
    }

    void BaseTxBuilder::CheckMinimumFee()
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
