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

#include "unlink_transaction.h"
#include "pull_transaction.h"
#include "push_transaction.h"
#include "core/shielded.h"
#include "push_tx_builder.h"
#include "wallet/core/strings_resources.h"

namespace beam::wallet::lelantus
{
    namespace
    {
        struct UnlinkTxBaseGateway : public INegotiatorGateway
        {
            UnlinkTxBaseGateway(UnlinkFundsTransaction& root)
                : m_Root(root)
            {

            }

            void OnAsyncStarted() override
            {
                m_Root.GetGateway().OnAsyncStarted();
            }
            
            void OnAsyncFinished() override
            {
                m_Root.GetGateway().OnAsyncFinished();
            }

            /*void on_tx_completed(const TxID&) override
            {
                
            }
            */
            void on_tx_failed(const TxID& txID) override
            {
                m_Root.GetGateway().on_tx_failed(txID);
            }

            void register_tx(const TxID& txID, Transaction::Ptr tx, SubTxID subTxID) override
            {
                m_Root.GetGateway().register_tx(txID, tx, subTxID);
            }

            void confirm_kernel(const TxID& txID, const Merkle::Hash& kernelID, SubTxID subTxID) override
            {
                m_Root.GetGateway().confirm_kernel(txID, kernelID, subTxID);
            }

            void confirm_asset(const TxID& txID, const PeerID& ownerID, SubTxID subTxID) override
            {
                m_Root.GetGateway().confirm_asset(txID, ownerID, subTxID);
            }

            void confirm_asset(const TxID& txID, const Asset::ID assetId, SubTxID subTxID) override
            {
                m_Root.GetGateway().confirm_asset(txID, assetId, subTxID);
            }

            void get_kernel(const TxID& txID, const Merkle::Hash& kernelID, SubTxID subTxID) override
            {
                m_Root.GetGateway().get_kernel(txID, kernelID, subTxID);
            }

            bool get_tip(Block::SystemState::Full& state) const override
            {
                return m_Root.GetGateway().get_tip(state);
            }

            void send_tx_params(const WalletID& peerID, const SetTxParameter& params) override
            {
                assert(false && "Shouldn't be used for unlink transaction");
            }

            void get_shielded_list(const TxID& txID, TxoID startIndex, uint32_t count, ShieldedListCallback&& callback) override
            {
                m_Root.GetGateway().get_shielded_list(txID, startIndex, count, std::move(callback));
            }

            void get_proof_shielded_output(const TxID& txID, const ECC::Point& serialPublic, ProofShildedOutputCallback&& callback) override
            {
                m_Root.GetGateway().get_proof_shielded_output(txID, serialPublic, std::move(callback));
            }

            void UpdateOnNextTip(const TxID& txID) override
            {
                m_Root.GetGateway().UpdateOnNextTip(txID);
            }

            UnlinkFundsTransaction& m_Root;
        };
    }

    TxParameters CreateUnlinkFundsTransactionParameters(const WalletID& myID, const boost::optional<TxID>& txId)
    {
        return CreateTransactionParameters(TxType::UnlinkFunds, txId)
            .SetParameter(TxParameterID::MyID, myID);
    }

    BaseTransaction::Ptr UnlinkFundsTransaction::Creator::Create(const TxContext& context)
    {
        return BaseTransaction::Ptr(new UnlinkFundsTransaction(context, m_withAssets));
    }

    TxParameters UnlinkFundsTransaction::Creator::CheckAndCompleteParameters(const TxParameters& parameters)
    {
        return parameters;
    }

    UnlinkFundsTransaction::UnlinkFundsTransaction(const TxContext& context
        , bool withAssets)
        : BaseTransaction(context)
        , m_withAssets(withAssets)
    {

    }

    TxType UnlinkFundsTransaction::GetType() const
    {
        return TxType::UnlinkFunds;
    }

    bool UnlinkFundsTransaction::IsInSafety() const
    {
        return true;
    }

    void UnlinkFundsTransaction::UpdateImpl()
    {
        auto state = GetState();
        switch(state)
        {
        case State::Initial:
            {
                auto fee = GetMandatoryParameter<Amount>(TxParameterID::Fee);
                auto amount = GetMandatoryParameter<Amount>(TxParameterID::Amount);
                if (amount <= fee)
                {
                    LOG_ERROR() << m_Context << "Cannot extract shielded coin, fee is to big.";
                    throw TransactionFailedException(false, TxFailureReason::ExtractFeeTooBig);
                }
                CreateInsertTransaction();
                UpdateActiveTransactions();
                SetState(State::Insertion);
            }
            break;
        case State::Insertion:
        case State::Extraction:
            UpdateActiveTransactions();
            break;
        case State::Unlinking:
            if (CheckAnonymitySet())
            {
                CreateExtractTransaction();
                UpdateActiveTransactions();
                SetState(State::Extraction);
            }
            else
            {
                UpdateOnNextTip();
            }
            break;
        }
    }
    
    void UnlinkFundsTransaction::RollbackTx()
    {
        LOG_INFO() << m_Context << " Transaction failed. Rollback...";
        GetWalletDB()->rollbackTx(GetTxID());
        GetWalletDB()->deleteShieldedCoinsCreatedByTx(GetTxID());
    }

    UnlinkFundsTransaction::State UnlinkFundsTransaction::GetState() const
    {
        State state = State::Initial;
        GetParameter(TxParameterID::State, state);
        return state;
    }

    void UnlinkFundsTransaction::UpdateActiveTransactions()
    {
        if (m_ActiveTransaction)
        {
            m_ActiveTransaction->Update();
        }
    }

    void UnlinkFundsTransaction::CreateInsertTransaction()
    {
        PushTransaction::Creator creator(m_withAssets);
        BaseTransaction::Creator& baseCreator = creator;

        struct PushTxGateway : public UnlinkTxBaseGateway
        {
            using UnlinkTxBaseGateway::UnlinkTxBaseGateway;
            void on_tx_completed(const TxID&) override
            {
                m_Root.SetState(State::Unlinking);
                m_Root.UpdateOnNextTip();

                m_Root.m_ActiveTransaction->FreeResources();
                m_Root.m_ActiveTransaction.reset();
            }
        };

        m_ActiveGateway = std::make_unique<PushTxGateway>(*this);
        auto tx = baseCreator.Create(TxContext(m_Context, *m_ActiveGateway, SubTxIndex::PUSH_TX));
        CopyParameter<AmountList>(TxParameterID::AmountList, *tx);
        CopyParameter<Amount>(TxParameterID::Amount, *tx);
        CopyParameter<Amount>(TxParameterID::Fee, *tx);
        CopyParameter<Height>(TxParameterID::MinHeight, *tx);
        CopyParameter<Height>(TxParameterID::MaxHeight, *tx);
        CopyParameter<Height>(TxParameterID::Lifetime, *tx);
        CopyParameter<WalletID>(TxParameterID::MyID, *tx);

        m_ActiveTransaction = tx;
    }

    bool UnlinkFundsTransaction::CheckAnonymitySet() const
    {
        auto coin = GetWalletDB()->getShieldedCoin(GetTxID());
        TxoID lastKnownShieldedOuts = 0;
        storage::getVar(*GetWalletDB(), kStateSummaryShieldedOutsDBPath, lastKnownShieldedOuts);
        auto targetAnonymitySet = Rules::get().Shielded.m_ProofMax.get_N();
        
        auto coinAnonymitySet = coin->GetAnonymitySet(lastKnownShieldedOuts);
        return (coinAnonymitySet >= targetAnonymitySet);
    }

    void UnlinkFundsTransaction::CreateExtractTransaction()
    {
        PullTransaction::Creator creator(m_withAssets);
        BaseTransaction::Creator& baseCreator = creator;

        struct PullTxGateway : public UnlinkTxBaseGateway
        {
            using UnlinkTxBaseGateway::UnlinkTxBaseGateway;
            void on_tx_completed(const TxID&) override
            {
                m_Root.CompleteTx();
            }
        };

        auto fee = GetMandatoryParameter<Amount>(TxParameterID::Fee);
        auto amount = GetMandatoryParameter<Amount>(TxParameterID::Amount);
        auto coin = GetWalletDB()->getShieldedCoin(GetTxID());
        assert(coin);
        assert(!m_ActiveTransaction);
        m_ActiveGateway = std::make_unique<PullTxGateway>(*this);
        auto tx = baseCreator.Create(TxContext(m_Context, *m_ActiveGateway, SubTxIndex::PULL_TX));

        tx->SetParameter(TxParameterID::Amount, amount - fee);
        CopyParameter<Amount>(TxParameterID::Fee, *tx);
        tx->SetParameter(TxParameterID::ShieldedOutputId, coin->m_ID);
        m_ActiveTransaction = tx;
    }
} // namespace beam::wallet::lelantus