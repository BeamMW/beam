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

            void register_tx(const TxID& txID, const Transaction::Ptr& tx, const Merkle::Hash* pParentCtx, SubTxID subTxID) override
            {
                m_Root.GetGateway().register_tx(txID, tx, pParentCtx, subTxID);
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
            .SetParameter(TxParameterID::MyAddr, myID);
    }

    BaseTransaction::Ptr UnlinkFundsTransaction::Creator::Create(const TxContext& context)
    {
        return BaseTransaction::Ptr(new UnlinkFundsTransaction(context));
    }

    TxParameters UnlinkFundsTransaction::Creator::CheckAndCompleteParameters(const TxParameters& parameters)
    {
        return parameters;
    }

    UnlinkFundsTransaction::UnlinkFundsTransaction(const TxContext& context)
        : BaseTransaction(TxType::UnlinkFunds, context)
    {

    }

    bool UnlinkFundsTransaction::Rollback(Height height)
    {
        if (m_ActiveTransaction)
        {
            return m_ActiveTransaction->Rollback(height);
        }
        return false;
    }

    void UnlinkFundsTransaction::Cancel() 
    {
        LOG_INFO() << "Canceling unlink transaction";
        const auto state = GetState<State>();
        switch (state)
        {
        case State::Initial:
            CompleteTx();
            break;
        case State::Insertion:
            if (m_ActiveTransaction->CanCancel())
            {
                m_ActiveTransaction->Cancel();
            }
            else
            {
                SetState(State::Cancellation);
            }
            break;
        case State::BeforeExtraction:
        case State::Cancellation:
        case State::Extraction:
            break;
        case State::Unlinking:
            SetState(State::BeforeExtraction);
            UpdateAsync();
            break;
        }
    }

    bool UnlinkFundsTransaction::IsInSafety() const
    {
        return true;
    }

    void UnlinkFundsTransaction::RollbackTx()
    {
    }

    void UnlinkFundsTransaction::UpdateImpl()
    {
        const auto state = GetState<State>();
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
                UpdateTxDescription(TxStatus::InProgress);
                UpdateActiveTransactions();
                SetState(State::Insertion);
            }
            break;
        case State::Insertion:
        case State::Extraction:
        case State::Cancellation:
            UpdateActiveTransactions();
            break;
        case State::BeforeExtraction:
            CreateExtractTransaction();
            UpdateActiveTransactions();
            SetState(State::Extraction);
            break;
        case State::Unlinking:
            if (CheckAnonymitySet())
            {
                SetState(State::BeforeExtraction);
                UpdateAsync();
            }
            else
            {
                UpdateOnNextTip();
            }
            break;
        }
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
        PushTransaction::Creator creator([this]() {return GetWalletDB(); });
        BaseTransaction::Creator& baseCreator = creator;

        struct PushTxGateway : public UnlinkTxBaseGateway
        {
            using UnlinkTxBaseGateway::UnlinkTxBaseGateway;
            void on_tx_completed(const TxID&) override
            {
                const auto state = m_Root.GetState<State>();
                switch (state)
                {
                case State::Insertion:
                    m_Root.SetState(State::Unlinking);
                    m_Root.UpdateOnNextTip();
                    break;
                case State::Cancellation:
                    m_Root.SetState(State::BeforeExtraction);
                    m_Root.UpdateAsync();
                    break;
                default:
                    LOG_ERROR() << "Unexpected state: " << int(state);
                    throw TransactionFailedException(false, TxFailureReason::Unknown);
                }

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
        CopyParameter<WalletID>(TxParameterID::MyAddr, *tx);

        m_ActiveTransaction = tx;
    }

    bool UnlinkFundsTransaction::CheckAnonymitySet() const
    {
        auto coin = GetWalletDB()->getShieldedCoin(GetTxID());
        ShieldedCoin& c = *coin;
        ShieldedCoin::UnlinkStatus us(c, GetWalletDB()->get_ShieldedOuts());
        return 100 == us.m_Progress;
    }

    void UnlinkFundsTransaction::CreateExtractTransaction()
    {
        PullTransaction::Creator creator;
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
        tx->SetParameter(TxParameterID::ShieldedOutputId, coin->m_TxoID);
        m_ActiveTransaction = tx;
    }
} // namespace beam::wallet::lelantus