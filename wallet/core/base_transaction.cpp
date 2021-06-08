// Copyright 2019 The Beam Team
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

#include "base_transaction.h"
#include "core/block_crypt.h"

// TODO: getrandom not available until API 28 in the Android NDK 17b
// https://github.com/boostorg/uuid/issues/76
#if defined(__ANDROID__)
#define BOOST_UUID_RANDOM_PROVIDER_DISABLE_GETRANDOM 1
#endif

#include <boost/uuid/uuid_generators.hpp>
#include <numeric>
#include "utility/logger.h"

namespace beam
{
    std::ostream& operator<<(std::ostream& os, const wallet::BaseTransaction::TxContext& context)
    {
        std::stringstream ss;
        ss << "[" << std::to_string(context.GetTxID()) << "]";
        if (context.GetSubTxID() != wallet::kDefaultSubTxID)
        {
            ss << "[" << std::to_string(context.GetSubTxID()) << "]";
        }
        os << ss.str();
        return os;
    }
}

namespace beam::wallet
{
    using namespace ECC;
    using namespace std;

    TxID GenerateTxID()
    {
        boost::uuids::uuid id = boost::uuids::random_generator()();
        TxID txID{};
        copy(id.begin(), id.end(), txID.begin());
        return txID;
    }

    TxParameters CreateTransactionParameters(TxType type, const boost::optional<TxID>& oTxId)
    {
        const auto txID = oTxId ? *oTxId : GenerateTxID();
        return TxParameters(txID)
            .SetParameter(TxParameterID::TransactionType, type)
            .SetParameter(TxParameterID::Lifetime, kDefaultTxLifetime)
            .SetParameter(TxParameterID::PeerResponseTime, kDefaultTxResponseTime)
            .SetParameter(TxParameterID::IsInitiator, true)
            .SetParameter(TxParameterID::IsSender, true)
            .SetParameter(TxParameterID::CreateTime, getTimestamp());
    }

    std::string GetFailureMessage(TxFailureReason reason)
    {
        switch (reason)
        {
#define MACRO(name, code, message) case name: return message;
            BEAM_TX_FAILURE_REASON_MAP(MACRO)
#undef MACRO
        }
        return "Unknown reason";
    }

    TransactionFailedException::TransactionFailedException(bool notify, TxFailureReason reason, const char* message)
        : std::runtime_error(message)
        , m_Notify{ notify }
        , m_Reason{ reason }
    {
    }

    bool TransactionFailedException::ShouldNofify() const
    {
        return m_Notify;
    }

    TxFailureReason TransactionFailedException::GetReason() const
    {
        return m_Reason;
    }

    const uint32_t BaseTransaction::s_ProtoVersion = 4;

    BaseTransaction::BaseTransaction(const TxType txType, const TxContext& context)
        : m_txType(txType)
        , m_Context{ context }
    {
        assert(context.GetWalletDB());
    }

    bool BaseTransaction::IsInitiator() const
    {
        if (!m_IsInitiator.is_initialized())
        {
            m_IsInitiator = GetMandatoryParameter<bool>(TxParameterID::IsInitiator);
        }
        return *m_IsInitiator;
    }

    bool BaseTransaction::GetTip(Block::SystemState::Full& state) const
    {
        return GetGateway().get_tip(state);
    }

    void BaseTransaction::UpdateAsync()
    {
        if (!m_EventToUpdate)
        {
            GetAsyncAcontext().OnAsyncStarted();
            m_EventToUpdate = io::AsyncEvent::create(io::Reactor::get_Current(), [this, weak = this->weak_from_this()]()
            { 
                auto eventHolder = m_EventToUpdate;
                if (auto tx = weak.lock())
                {
                    Update();
                    GetAsyncAcontext().OnAsyncFinished();
                }
            });
            m_EventToUpdate->post();
        }
    }

    const TxID& BaseTransaction::GetTxID() const
    {
        return m_Context.GetTxID();
    }

    void BaseTransaction::Update()
    {
        AsyncContextHolder async(m_Context.GetGateway());
        try
        {
            m_EventToUpdate.reset();
            if (CheckExternalFailures())
            {
                return;
            }

            UpdateImpl();

            CheckExpired();
            SetParameter(TxParameterID::ModifyTime, getTimestamp(), true);
        }
        catch (const TransactionFailedException & ex)
        {
            if (ex.what() && strlen(ex.what()))
            {
                LOG_ERROR() << m_Context << " exception msg: " << ex.what();
            }
            OnFailed(ex.GetReason(), ex.ShouldNofify());
        }
        catch (const exception & ex)
        {
            if (ex.what() && strlen(ex.what()))
            {
                LOG_ERROR() << m_Context << " exception msg: " << ex.what();
            }
            OnFailed(TxFailureReason::Unknown);
        }
    }

    bool BaseTransaction::CanCancel() const
    {
        TxStatus status = TxStatus::Failed;
        GetParameter(TxParameterID::Status, status);

        return status == TxStatus::InProgress || status == TxStatus::Pending;
    }

    void BaseTransaction::Cancel()
    {
        TxStatus s = TxStatus::Failed;
        GetParameter(TxParameterID::Status, s);
        // TODO: add CanCancel() method
        if (s == TxStatus::Pending || s == TxStatus::InProgress)
        {
            if (s == TxStatus::InProgress)
            {
                // notify about cancellation if we have started negotiations
                NotifyFailure(TxFailureReason::Canceled);

            }
            UpdateTxDescription(TxStatus::Canceled);
            RollbackTx();
            GetGateway().on_tx_failed(GetTxID());
        }
        else
        {
            LOG_INFO() << m_Context << " You cannot cancel transaction in state: " << static_cast<int>(s);
        }
    }

    bool BaseTransaction::Rollback(Height height)
    {
        Height proofHeight;

        if (GetParameter(TxParameterID::KernelProofHeight, proofHeight) && (proofHeight > height))
        {
            SetParameter(TxParameterID::Status, TxStatus::Registering);
            SetParameter(TxParameterID::KernelProofHeight, Height(0));
            SetParameter(TxParameterID::KernelUnconfirmedHeight, Height(0));
            return true;
        }
        return false;
    }

    void BaseTransaction::RollbackTx()
    {
        LOG_INFO() << m_Context << " Transaction failed. Rollback...";
        m_Context.GetWalletDB()->restoreCoinsSpentByTx(GetTxID());
        m_Context.GetWalletDB()->deleteCoinsCreatedByTx(GetTxID());
        m_Context.GetWalletDB()->restoreShieldedCoinsSpentByTx(GetTxID());
    }

    INegotiatorGateway& BaseTransaction::GetGateway() const
    {
        return m_Context.GetGateway();
    }

    SubTxID BaseTransaction::GetSubTxID() const
    {
        return m_Context.GetSubTxID();
    }

    bool BaseTransaction::CheckExpired()
    {
        TxStatus s = TxStatus::Failed;
        if (GetParameter(TxParameterID::Status, s)
            && (s == TxStatus::Failed
                || s == TxStatus::Canceled
                || s == TxStatus::Completed))
        {
            return false;
        }

        Height maxHeight = MaxHeight;
        if (!GetParameter(TxParameterID::MaxHeight, maxHeight)
            && !GetParameter(TxParameterID::PeerResponseHeight, maxHeight))
        {
            // we have no data to make decision, but we can use kernels maximum life time from the rules
            if (GetParameter(TxParameterID::MinHeight, maxHeight))
            {
                maxHeight += Rules::get().MaxKernelValidityDH;
            }
            else
            {
                return false;
            }
            
        }

        uint8_t nRegistered = proto::TxStatus::Unspecified;
        Merkle::Hash kernelID;
        bool isSender = GetMandatoryParameter<bool>(TxParameterID::IsSender);
        if ((!GetParameter(TxParameterID::TransactionRegistered, nRegistered) && isSender)
            || !GetParameter(TxParameterID::KernelID, kernelID))
        {
            Block::SystemState::Full state;
            if (GetTip(state) && state.m_Height > maxHeight)
            {
                LOG_INFO() << m_Context << " Transaction expired. Current height: " << state.m_Height << ", max kernel height: " << maxHeight;
                OnFailed(TxFailureReason::TransactionExpired);
                return true;
            }
        }
        else
        {
            Height lastUnconfirmedHeight = 0;
            if (GetParameter(TxParameterID::KernelUnconfirmedHeight, lastUnconfirmedHeight) && lastUnconfirmedHeight > 0)
            {
                if (lastUnconfirmedHeight >= maxHeight)
                {
                    LOG_INFO() << m_Context << " Transaction expired. Last unconfirmeed height: " << lastUnconfirmedHeight << ", max kernel height: " << maxHeight;
                    OnFailed(TxFailureReason::TransactionExpired);
                    return true;
                }
            }
        }
        return false;
    }

    bool BaseTransaction::CheckExternalFailures()
    {
        TxFailureReason reason = TxFailureReason::Unknown;
        if (!GetParameter(TxParameterID::FailureReason, reason))
            return false; // there is no failure messages

        TxStatus s = GetMandatoryParameter<TxStatus>(TxParameterID::Status);
        if (s == TxStatus::InProgress || s == TxStatus::Pending)
        {
            if (reason == TxFailureReason::AssetsDisabledInWallet)
            {
                bool isSender = false;
                if (GetParameter(TxParameterID::IsSender, isSender) && isSender)
                {
                    // overwrite the reason if it was sent by old client
                    reason = TxFailureReason::AssetsDisabledReceiver;
                }
            }
                
            OnFailed(reason);
            return true;
        }
        else if (s == TxStatus::Failed || s == TxStatus::Canceled)
        {
            // this tx has been already failed
            return true;
        }
        // at this moment we ignore all failure messages
        return false;
    }

    void BaseTransaction::ConfirmKernel(const Merkle::Hash& kernelID)
    {
        UpdateTxDescription(TxStatus::Registering);
        GetGateway().confirm_kernel(GetTxID(), kernelID, m_Context.GetSubTxID());
    }

    void BaseTransaction::UpdateOnNextTip()
    {
        GetGateway().UpdateOnNextTip(GetTxID());
    }

    void BaseTransaction::CompleteTx()
    {
        auto minConfirmations = GetWalletDB()->getCoinConfirmationsOffset();
        if (minConfirmations)
        {
            Height hProof = 0;
            if (GetParameter<Height>(TxParameterID::KernelProofHeight, hProof) && hProof)
            {
                auto currHeight = GetWalletDB()->getCurrentHeight();
                if (currHeight - hProof < minConfirmations)
                {
                    UpdateTxDescription(TxStatus::Confirming);
                    GetGateway().UpdateOnNextTip(GetTxID());
                    return;
                }
            }
        }

        LOG_INFO() << m_Context << " Transaction completed";
        UpdateTxDescription(TxStatus::Completed);
        GetGateway().on_tx_completed(GetTxID());
    }

    void BaseTransaction::UpdateTxDescription(TxStatus s)
    {
        SetParameter(TxParameterID::Status, s, true, m_Context.GetSubTxID());
    }

    void BaseTransaction::OnFailed(TxFailureReason reason, bool notify)
    {
        LOG_ERROR() << m_Context << " Failed. " << GetFailureMessage(reason);

        if (notify)
        {
            NotifyFailure(reason);
        }

        SetParameter(TxParameterID::FailureReason, reason, false);
        UpdateTxDescription((reason == TxFailureReason::Canceled) ? TxStatus::Canceled : TxStatus::Failed);
        RollbackTx();

        GetGateway().on_tx_failed(GetTxID());
    }

    IPrivateKeyKeeper2::Slot::Type BaseTransaction::GetSlotSafe(bool bAllocateIfAbsent)
    {
        IPrivateKeyKeeper2::Slot::Type iSlot = IPrivateKeyKeeper2::Slot::Invalid;
        GetParameter(TxParameterID::NonceSlot, iSlot);

        if (bAllocateIfAbsent && (IPrivateKeyKeeper2::Slot::Invalid == iSlot))
        {
            iSlot = GetWalletDB()->SlotAllocate();

            if (IPrivateKeyKeeper2::Slot::Invalid == iSlot)
                throw TransactionFailedException(true, TxFailureReason::KeyKeeperNoSlots);

            SetParameter(TxParameterID::NonceSlot, iSlot);
        }

        return iSlot;
    }

    void BaseTransaction::FreeSlotSafe()
    {
        IPrivateKeyKeeper2::Slot::Type iSlot = GetSlotSafe(false);
        if (IPrivateKeyKeeper2::Slot::Invalid != iSlot)
        {
            m_Context.GetWalletDB()->SlotFree(iSlot);
            SetParameter(TxParameterID::NonceSlot, IPrivateKeyKeeper2::Slot::Invalid);
        }
    }

    void BaseTransaction::FreeResources()
    {
        FreeSlotSafe(); // if was used
    }

    void BaseTransaction::NotifyFailure(TxFailureReason reason)
    {
        TxStatus s = TxStatus::Failed;
        GetParameter(TxParameterID::Status, s);

        switch (s)
        {
        case TxStatus::Pending:
        case TxStatus::InProgress:
            // those are the only applicable statuses, where there's no chance tx can be valid
            break;
        default:
            return;
        }

        SetTxParameter msg;
        msg.AddParameter(TxParameterID::FailureReason, reason);
        SendTxParameters(move(msg));
    }

    IWalletDB::Ptr BaseTransaction::GetWalletDB() const
    {
        return m_Context.GetWalletDB();
    }

    IPrivateKeyKeeper2::Ptr BaseTransaction::get_KeyKeeperStrict()
    {
        IPrivateKeyKeeper2::Ptr ret = m_Context.GetWalletDB()->get_KeyKeeper();
        if (!ret)
            throw TransactionFailedException(true, TxFailureReason::NoKeyKeeper);

        return ret;
    }

    Key::IKdf::Ptr BaseTransaction::get_MasterKdfStrict() const
    {
        Key::IKdf::Ptr ret = m_Context.GetWalletDB()->get_MasterKdf();
        if (!ret)
            throw TransactionFailedException(true, TxFailureReason::NoMasterKey);

        return ret;
    }

    void BaseTransaction::TestKeyKeeperRet(IPrivateKeyKeeper2::Status::Type n)
    {
        if (IPrivateKeyKeeper2::Status::Success != n)
            throw TransactionFailedException(true, KeyKeeperErrorToFailureReason(n));
    }

    TxFailureReason BaseTransaction::KeyKeeperErrorToFailureReason(IPrivateKeyKeeper2::Status::Type n)
    {
        if (IPrivateKeyKeeper2::Status::UserAbort == n)
            return TxFailureReason::KeyKeeperUserAbort;

        return TxFailureReason::KeyKeeperError;
    }

    IAsyncContext& BaseTransaction::GetAsyncAcontext() const
    {
        return GetGateway();
    }

    bool BaseTransaction::SendTxParameters(SetTxParameter && msg) const
    {
        msg.m_TxID = GetTxID();
        msg.m_Type = GetType();

        WalletID peerID;
        if (GetParameter(TxParameterID::MyID, msg.m_From)
            && GetParameter(TxParameterID::PeerID, peerID))
        { 
            PeerID secureWalletID = Zero, peerWalletID = Zero;
            if (GetParameter(TxParameterID::MyWalletIdentity, secureWalletID) 
             && GetParameter(TxParameterID::PeerWalletIdentity, peerWalletID))
            {
                msg.AddParameter(TxParameterID::PeerWalletIdentity, secureWalletID);
            }
            GetGateway().send_tx_params(peerID, msg);
            return true;
        }
        return false;
    }

    void BaseTransaction::SetCompletedTxCoinStatuses(Height proofHeight)
    {
        auto walletDb = GetWalletDB();
        std::vector<Coin> modified = walletDb->getCoinsByTx(GetTxID());
        for (auto& coin : modified)
        {
            bool bIn = (coin.m_createTxId && *coin.m_createTxId == GetTxID());
            bool bOut = (coin.m_spentTxId && *coin.m_spentTxId == GetTxID());
            if (bIn || bOut)
            {
                if (bIn)
                {
                    std::setmin(coin.m_confirmHeight, proofHeight);
                    coin.m_maturity = proofHeight + Rules::get().Maturity.Std; // so far we don't use incubation for our created outputs
                }
                if (bOut)
                {
                    std::setmin(coin.m_spentHeight, proofHeight);
                }
            }
        }

        walletDb->saveCoins(modified);

        std::vector<IPrivateKeyKeeper2::ShieldedInput> inputShielded;
        GetParameter(TxParameterID::InputCoinsShielded, inputShielded);

        if (!inputShielded.empty())
        {
            Block::SystemState::Full sTip;
            walletDb->get_History().get_Tip(sTip);

            for(const auto& coin : inputShielded)
            {
                auto shieldedCoin = walletDb->getShieldedCoin(coin.m_Key);
                if (shieldedCoin)
                {
                    shieldedCoin->m_spentTxId = GetTxID();
                    shieldedCoin->m_spentHeight = sTip.m_Height;
                    shieldedCoin->m_Status = ShieldedCoin::Status::Spent;
                    walletDb->saveShieldedCoin(*shieldedCoin);
                }
            }
        }
    }

    void BaseTransaction::LogFailedParameter(TxParameterID paramID, SubTxID subTxID) const
    {
        LOG_ERROR() << GetTxID() << "[" << subTxID << "] Failed to get parameter: " << (int)paramID;
    }

    BaseTransaction::AssetCheckResult BaseTransaction::CheckAsset(Asset::ID assetId)
    {
        if (assetId == Asset::s_InvalidID)
        {
            return AssetCheckResult::OK;
        }

        //
        // Do not do anything if we've already failed
        //
        Height ucHeight = 0;
        if (GetParameter(TxParameterID::AssetUnconfirmedHeight, ucHeight) && ucHeight != 0)
        {
            OnFailed(TxFailureReason::AssetConfirmFailed);
            return AssetCheckResult::Fail;
        }

        if (m_assetCheckState.find(assetId) == m_assetCheckState.end())
        {
            m_assetCheckState[assetId] = AssetCheckState::ACInitial;
        }

        const auto confirmAsset = [&]() {
            m_assetCheckState[assetId] = ACConfirmation;
            SetParameter(TxParameterID::AssetInfoFull, Asset::Full());
            SetParameter(TxParameterID::AssetUnconfirmedHeight, Height(0));
            SetParameter(TxParameterID::AssetConfirmedHeight, Height(0));
            GetGateway().confirm_asset(GetTxID(), assetId, kDefaultSubTxID);
        };

        bool printInfo = true;
        if (m_assetCheckState[assetId] == ACInitial)
        {
            const auto pInfo = GetWalletDB()->findAsset(assetId);
            if (!pInfo)
            {
                confirmAsset();
                return AssetCheckResult::Async;
            }

            SetParameter(TxParameterID::AssetInfoFull, Cast::Down<Asset::Full>(*pInfo));
            SetParameter(TxParameterID::AssetConfirmedHeight, pInfo->m_RefreshHeight);
            m_assetCheckState[assetId] = ACCheck;
        }

        if (m_assetCheckState[assetId] == ACConfirmation)
        {
            Height acHeight = 0;
            if (!GetParameter(TxParameterID::AssetConfirmedHeight, acHeight) || acHeight == 0)
            {
                // we're still in progress
                return AssetCheckResult::Async;
            }

            m_assetCheckState[assetId] = ACCheck;
            printInfo = false;
        }

        if (m_assetCheckState[assetId] == ACCheck)
        {
            Asset::Full infoFull;
            if (!GetParameter(TxParameterID::AssetInfoFull, infoFull) || !infoFull.IsValid())
            {
                OnFailed(TxFailureReason::NoAssetInfo, true);
                return AssetCheckResult::Fail;
            }

            Height acHeight = 0;
            if(!GetParameter(TxParameterID::AssetConfirmedHeight, acHeight) || acHeight == 0)
            {
                OnFailed(TxFailureReason::NoAssetInfo, true);
                return AssetCheckResult::Fail;
            }

            const auto currHeight = GetWalletDB()->getCurrentHeight();
            WalletAsset info(infoFull, acHeight);

            if (info.CanRollback(currHeight))
            {
                OnFailed(TxFailureReason::AssetLocked, true);
                return AssetCheckResult::Fail;
            }

            if (info.IsExpired(*GetWalletDB()))
            {
                confirmAsset();
                return AssetCheckResult::Async;
            }

            if (printInfo)
            {
                if (const auto& asset = GetWalletDB()->findAsset(assetId))
                {
                    asset->LogInfo(GetTxID(), GetSubTxID());
                }
            }

            return AssetCheckResult::OK;
        }

        assert(!"Wrong logic in SimpleTransaction::CheckAsset");
        OnFailed(TxFailureReason::Unknown, true);
        return AssetCheckResult::Fail;
    }

    bool BaseTransaction::IsSelfTx() const
    {
        const auto peerID = GetMandatoryParameter<WalletID>(TxParameterID::PeerID);
        const auto address = GetWalletDB()->getAddress(peerID);
        return address.is_initialized() && address->isOwn();
    }
}
