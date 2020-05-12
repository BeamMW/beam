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

#include "simple_transaction.h"

#include "base_tx_builder.h"
#include "wallet.h"
#include "core/block_crypt.h"
#include "strings_resources.h"

#include <numeric>
#include "utility/logger.h"

namespace beam::wallet
{
    using namespace ECC;
    using namespace std;

    TxParameters CreateSimpleTransactionParameters(const boost::optional<TxID>& txId)
    {
        return CreateTransactionParameters(TxType::Simple, txId);
    }

    TxParameters CreateSplitTransactionParameters(const WalletID& myID, const AmountList& amountList, const boost::optional<TxID>& txId)
    {
        return CreateSimpleTransactionParameters(txId)
            .SetParameter(TxParameterID::MyID, myID)
            .SetParameter(TxParameterID::PeerID, myID)
            .SetParameter(TxParameterID::AmountList, amountList)
            .SetParameter(TxParameterID::Amount, std::accumulate(amountList.begin(), amountList.end(), Amount(0)));
    }

    SimpleTransaction::Creator::Creator(IWalletDB::Ptr walletDB)
        : m_WalletDB(walletDB)
    {

    }

    BaseTransaction::Ptr SimpleTransaction::Creator::Create(INegotiatorGateway& gateway
                                                          , IWalletDB::Ptr walletDB
                                                          , const TxID& txID)
    {
        return BaseTransaction::Ptr(new SimpleTransaction(gateway, walletDB, txID));
    }

    TxParameters SimpleTransaction::Creator::CheckAndCompleteParameters(const TxParameters& parameters)
    {
        const auto& peerID = parameters.GetParameter<WalletID>(TxParameterID::PeerID);
        if (!peerID)
        {
            throw InvalidTransactionParametersException("No PeerID");
        }

        auto receiverAddr = m_WalletDB->getAddress(*peerID);
        if (receiverAddr)
        {
            if (receiverAddr->isOwn() && receiverAddr->isExpired())
            {
                LOG_INFO() << "Can't send to the expired address.";
                throw AddressExpiredException();
            }

            // update address comment if changed
            if (auto message = parameters.GetParameter(TxParameterID::Message); message)
            {
                auto messageStr = std::string(message->begin(), message->end());
                if (messageStr != receiverAddr->m_label)
                {
                    receiverAddr->m_label = messageStr;
                    m_WalletDB->saveAddress(*receiverAddr);
                }
            }          

            TxParameters temp{ parameters };
            temp.SetParameter(TxParameterID::IsSelfTx, receiverAddr->isOwn());
            return temp;
        }
        else
        {
            WalletAddress address;
            address.m_walletID = *peerID;
            address.m_createTime = getTimestamp();
            if (auto message = parameters.GetParameter(TxParameterID::Message); message)
            {
                address.m_label = std::string(message->begin(), message->end());
            }
            if (auto identity = parameters.GetParameter<PeerID>(TxParameterID::PeerSecureWalletID); identity)
            {
                address.m_Identity = *identity;
            }

            m_WalletDB->saveAddress(address);
        }
        return parameters;
    }

    SimpleTransaction::SimpleTransaction(INegotiatorGateway& gateway
                                       , IWalletDB::Ptr walletDB
                                       , const TxID& txID)
        : BaseTransaction{ gateway, walletDB, txID }
    {

    }

    TxType SimpleTransaction::GetType() const
    {
        return TxType::Simple;
    }

    bool SimpleTransaction::IsInSafety() const
    {
        State txState = GetState();
        return txState == State::KernelConfirmation;
    }

    void SimpleTransaction::UpdateImpl()
    {
        const bool isSender = GetMandatoryParameter<bool>(TxParameterID::IsSender);
        const bool isSelfTx = IsSelfTx();
        const auto txState  = GetState();
        AmountList amoutList;
        if (!GetParameter(TxParameterID::AmountList, amoutList))
        {
            amoutList = AmountList{ GetMandatoryParameter<Amount>(TxParameterID::Amount) };
        }

        if (!m_TxBuilder)
        {
            m_TxBuilder = make_shared<BaseTxBuilder>(*this, kDefaultSubTxID, amoutList, GetMandatoryParameter<Amount>(TxParameterID::Fee));
        }
        auto sharedBuilder = m_TxBuilder;
        BaseTxBuilder& builder = *sharedBuilder;
        builder.GetPeerInputsAndOutputs();
        const bool isAsset = builder.GetAssetId() != Asset::s_InvalidID;

        // Check if we already have signed kernel
        if ((isSender && !builder.LoadKernel())
         || (!isSender && (!builder.HasKernelID() || txState == State::Initial)))
        {
            // We need key keeper initialized to go on beyond this point
            if (!m_WalletDB->get_KeyKeeper())
            {
                // public wallet
                return;
            }

            uint64_t nAddrOwnID;
            if (!GetParameter(TxParameterID::MyAddressID, nAddrOwnID))
            {
                WalletID wid;
                if (GetParameter(TxParameterID::MyID, wid))
                {
                    auto waddr = m_WalletDB->getAddress(wid);
                    if (waddr && waddr->isOwn())
                    {
                        SetParameter(TxParameterID::MyAddressID, waddr->m_OwnID);
                        SetParameter(TxParameterID::MySecureWalletID, waddr->m_Identity);
                    }
                }
            }

            if (!builder.GetInitialTxParams() && txState == State::Initial)
            {
                PeerID myWalletID, peerWalletID;
                bool hasID = GetParameter<PeerID>(TxParameterID::MySecureWalletID, myWalletID)
                    && GetParameter<PeerID>(TxParameterID::PeerSecureWalletID, peerWalletID);
                stringstream ss;
                ss << GetTxID() << (isSender ? " Sending " : " Receiving ")
                    << PrintableAmount(builder.GetAmount(), false,isAsset ? kAmountASSET : "", isAsset ? kAmountAGROTH : "")
                    << " (fee: " << PrintableAmount(builder.GetFee()) << ")";

                if (isAsset)
                {
                    ss << ", asset ID: " << builder.GetAssetId();
                }

                if (hasID)
                {
                    ss << ", my ID: " << myWalletID << ", peer ID: " << peerWalletID;
                }
                LOG_INFO() << ss.str();

                UpdateTxDescription(TxStatus::InProgress);

                if (isSender)
                {
                    Height maxResponseHeight = 0;
                    if (GetParameter(TxParameterID::PeerResponseHeight, maxResponseHeight))
                    {
                        LOG_INFO() << GetTxID() << " Max height for response: " << maxResponseHeight;
                    }

                    builder.SelectInputs();
                    builder.AddChange();
                    builder.GenerateNonce();
                }

                if (isSelfTx || !isSender)
                {
                    // create receiver utxo
                    for (const auto& amount : builder.GetAmountList())
                    {
                        if (builder.GetAssetId() != 0)
                        {
                            builder.GenerateAssetCoin(amount, false);
                        }
                        else
                        {
                            builder.GenerateBeamCoin(amount, false);
                        }
                    }
                }
            }

            bool bI = builder.CreateInputs();
            bool bO = builder.CreateOutputs();
            if (bI || bO)
                return;

            if (!isSelfTx && !builder.GetPeerPublicExcessAndNonce())
            {
                assert(IsInitiator());
                if (txState == State::Initial)
                {
                    if (builder.SignSender(true))
                        return;

                    SendInvitation(builder, isSender);
                    SetState(State::Invitation);
                }
                UpdateOnNextTip();
                return;
            }

            if (!builder.UpdateMaxHeight())
            {
                OnFailed(TxFailureReason::MaxHeightIsUnacceptable, true);
                return;
            }

            builder.CreateKernel();

            if (!isSelfTx && !builder.GetPeerSignature())
            {
                if (txState == State::Initial)
                {
                    // invited participant
                    assert(!IsInitiator());

                    if (builder.SignReceiver())
                        return;

                    //
                    //  Since operations above can take undefined amount of time here is the only place
                    //  where we can check that it is OK to receive this particular asset
                    //  and expect that it would not be changed / manipulated
                    //
                    if(CheckAsset(builder.GetAssetId()) != AssetCheckResult::OK)
                    {
                        // can be request for async operation or simple fail
                        // in both cases we should jump out of here
                        return;
                    }

                    UpdateTxDescription(TxStatus::Registering);
                    ConfirmInvitation(builder);

                    uint32_t nVer = 0;
                    if (GetParameter(TxParameterID::PeerProtoVersion, nVer))
                    {
                        // for peers with new flow, we assume that after we have responded, we have to switch to the state of awaiting for proofs
						uint8_t nCode = proto::TxStatus::Ok; // compiler workaround (ref to static const)
						SetParameter(TxParameterID::TransactionRegistered, nCode);

                        SetState(State::KernelConfirmation);
                        ConfirmKernel(builder.GetKernelID());
                    }
                    else
                    {
                        SetState(State::InvitationConfirmation);
                    }
                    return;
                }
                if (IsInitiator())
                {
                    return;
                }
            }

            if (!isSelfTx)
            {
                if (builder.SignSender(false))
                    return;
            }
            else
            {
                if (builder.SignSplit())
                    return;
            }

            if (IsInitiator() && !builder.IsPeerSignatureValid())
            {
                OnFailed(TxFailureReason::InvalidPeerSignature, true);
                return;
            }

            builder.FinalizeSignature();
        }

        uint8_t nRegistered = proto::TxStatus::Unspecified;
        if (!GetParameter(TxParameterID::TransactionRegistered, nRegistered))
        {
            if (CheckExpired())
            {
                return;
            }

            // Construct transaction
            auto transaction = builder.CreateTransaction();

            // Verify final transaction
            TxBase::Context::Params pars;
			TxBase::Context ctx(pars);
			ctx.m_Height.m_Min = builder.GetMinHeight();
			if (!transaction->IsValid(ctx))
            {
                OnFailed(TxFailureReason::InvalidTransaction, true);
                return;
            }

			GetGateway().register_tx(GetTxID(), transaction);
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

        Height hProof = 0;
        GetParameter(TxParameterID::KernelProofHeight, hProof);
        if (!hProof)
        {
            SetState(State::KernelConfirmation);
            ConfirmKernel(builder.GetKernelID());
            return;
        }

        SetCompletedTxCoinStatuses(hProof);
        CompleteTx();
    }

    SimpleTransaction::AssetCheckResult SimpleTransaction::CheckAsset(Asset::ID assetId)
    {
        if (assetId == Asset::s_InvalidID)
        {
            // No asset - no error
            return AssetCheckResult::OK;
        }

        const auto confirmAsset = [&]() {
            m_assetCheckState = ACConfirmation;
            SetParameter(TxParameterID::AssetInfoFull, Asset::Full());
            SetParameter(TxParameterID::AssetUnconfirmedHeight, Height(0));
            SetParameter(TxParameterID::AssetConfirmedHeight, Height(0));
            GetGateway().confirm_asset(GetTxID(), assetId, kDefaultSubTxID);
        };

        if (m_assetCheckState == ACInitial)
        {
            if (const auto oinfo = m_WalletDB->findAsset(assetId))
            {
                SetParameter(TxParameterID::AssetInfoFull, static_cast<Asset::Full>(*oinfo));
                SetParameter(TxParameterID::AssetConfirmedHeight, oinfo->m_RefreshHeight);
                m_assetCheckState = ACCheck;
            }
            else
            {
                confirmAsset();
                return AssetCheckResult::Async;
            }
        }

        if (m_assetCheckState == ACConfirmation)
        {
            Height auHeight = 0;
            GetParameter(TxParameterID::AssetUnconfirmedHeight, auHeight);
            if (auHeight)
            {
                OnFailed(TxFailureReason::AssetConfirmFailed);
                return AssetCheckResult::Fail;
            }

            Height acHeight = 0;
            GetParameter(TxParameterID::AssetConfirmedHeight, acHeight);
            if (!acHeight)
            {
                GetGateway().confirm_asset(GetTxID(), assetId, kDefaultSubTxID);
                return AssetCheckResult::Async;
            }

            m_assetCheckState = ACCheck;
        }

        if (m_assetCheckState == ACCheck)
        {
            Asset::Full infoFull;
            if (!GetParameter(TxParameterID::AssetInfoFull, infoFull) || !infoFull.IsValid())
            {
                OnFailed(TxFailureReason::NoAssetInfo);
                return AssetCheckResult::Fail;
            }

            Height acHeight = 0;
            if(!GetParameter(TxParameterID::AssetConfirmedHeight, acHeight) || !acHeight)
            {
                OnFailed(TxFailureReason::NoAssetInfo);
                return AssetCheckResult::Fail;
            }

            const auto currHeight = m_WalletDB->getCurrentHeight();
            WalletAsset info(infoFull, acHeight);

            if (info.CanRollback(currHeight))
            {
                OnFailed(TxFailureReason::AssetLocked, true);
                return AssetCheckResult::Fail;
            }

            const auto getRange = [](WalletAsset& info) -> auto {
                HeightRange result;
                result.m_Min = info.m_LockHeight;
                result.m_Max = AmountBig::get_Lo(info.m_LockHeight) > 0 ? info.m_RefreshHeight : info.m_LockHeight;
                result.m_Max += Rules::get().CA.LockPeriod;
                return result;
            };

            HeightRange hrange = getRange(info);
            if (info.m_Value > AmountBig::Type(0U))
            {
                m_WalletDB->visitCoins([&](const Coin& coin) -> bool {
                    if (coin.m_ID.m_AssetID != assetId) return true;
                    if (coin.m_confirmHeight > hrange.m_Max) return true;
                    if (coin.m_confirmHeight < hrange.m_Min) return true;

                    const Height h1 = coin.m_spentHeight != MaxHeight ? coin.m_spentHeight : currHeight;
                    if (info.m_RefreshHeight < h1)
                    {
                        info.m_RefreshHeight = h1;
                        hrange = getRange(info);
                    }
                    return true;
                });
            }

            if (!hrange.IsInRange(currHeight) || hrange.m_Max < currHeight)
            {
                confirmAsset();
                return AssetCheckResult::Async;
            }

            return AssetCheckResult::OK;
        }

        assert(!"Wrong logic in SimpleTransaction::CheckAsset");
        OnFailed(TxFailureReason::Unknown);
        return AssetCheckResult::Fail;
    }

    void SimpleTransaction::SendInvitation(const BaseTxBuilder& builder, bool isSender)
    {
        SetTxParameter msg;
        msg.AddParameter(TxParameterID::Amount, builder.GetAmount())
            .AddParameter(TxParameterID::Fee, builder.GetFee())
            .AddParameter(TxParameterID::MinHeight, builder.GetMinHeight())
            .AddParameter(TxParameterID::Lifetime, builder.GetLifetime())
            .AddParameter(TxParameterID::PeerMaxHeight, builder.GetMaxHeight())
            .AddParameter(TxParameterID::IsSender, !isSender)
            .AddParameter(TxParameterID::PeerProtoVersion, s_ProtoVersion)
            .AddParameter(TxParameterID::PeerPublicExcess, builder.GetPublicExcess())
            .AddParameter(TxParameterID::PeerPublicNonce, builder.GetPublicNonce())
            .AddParameter(TxParameterID::AssetID, builder.GetAssetId());

        if (!SendTxParameters(move(msg)))
        {
            OnFailed(TxFailureReason::FailedToSendParameters, false);
        }
    }

    void SimpleTransaction::ConfirmInvitation(const BaseTxBuilder& builder)
    {
        LOG_INFO() << GetTxID() << " Transaction accepted. Kernel: " << builder.GetKernelIDString();
        SetTxParameter msg;
        msg
            .AddParameter(TxParameterID::PeerProtoVersion, s_ProtoVersion)
            .AddParameter(TxParameterID::PeerPublicExcess, builder.GetPublicExcess())
            .AddParameter(TxParameterID::PeerSignature, builder.GetPartialSignature())
            .AddParameter(TxParameterID::PeerPublicNonce, builder.GetPublicNonce())
            .AddParameter(TxParameterID::PeerMaxHeight, builder.GetMaxHeight())
            .AddParameter(TxParameterID::PeerInputs, builder.GetInputs())
            .AddParameter(TxParameterID::PeerOutputs, builder.GetOutputs())
            .AddParameter(TxParameterID::PeerOffset, builder.GetOffset());

        assert(!IsSelfTx());
        if (!GetMandatoryParameter<bool>(TxParameterID::IsSender))
        {
            Signature paymentProofSignature;
            if (GetParameter(TxParameterID::PaymentConfirmation, paymentProofSignature))
            {
                msg.AddParameter(TxParameterID::PaymentConfirmation, paymentProofSignature);
            }
            else
            {
                PaymentConfirmation pc;
                WalletID widPeer, widMy;
                bool bSuccess =
                    GetParameter(TxParameterID::PeerID, widPeer) &&
                    GetParameter(TxParameterID::MyID, widMy) &&
                    GetParameter(TxParameterID::KernelID, pc.m_KernelID) &&
                    GetParameter(TxParameterID::Amount, pc.m_Value);
                    // TODO:ASSETS check if need to add to the success check
                    GetParameter(TxParameterID::AssetID, pc.m_AssetID);

                if (bSuccess)
                {
                    pc.m_Sender = widPeer.m_Pk;

                    auto waddr = m_WalletDB->getAddress(widMy);
                    if (waddr && waddr->isOwn())
                    {
                        Scalar::Native sk;
                        m_WalletDB->get_SbbsPeerID(sk, widMy.m_Pk, waddr->m_OwnID);

                        pc.Sign(sk);
                        msg.AddParameter(TxParameterID::PaymentConfirmation, pc.m_Signature);
                    }
                }
            }
        }

        SendTxParameters(move(msg));
    }

    void SimpleTransaction::NotifyTransactionRegistered()
    {
        SetTxParameter msg;
		uint8_t nCode = proto::TxStatus::Ok; // compiler workaround (ref to static const)
        msg.AddParameter(TxParameterID::TransactionRegistered, nCode);
        SendTxParameters(move(msg));
    }

    bool SimpleTransaction::IsSelfTx() const
    {
        WalletID peerID = GetMandatoryParameter<WalletID>(TxParameterID::PeerID);
        auto address = m_WalletDB->getAddress(peerID);
        return address.is_initialized() && address->isOwn();
    }

    SimpleTransaction::State SimpleTransaction::GetState() const
    {
        State state = State::Initial;
        GetParameter(TxParameterID::State, state);
        return state;
    }

    bool SimpleTransaction::ShouldNotifyAboutChanges(TxParameterID paramID) const
    {
        switch (paramID)
        {
        case TxParameterID::Amount:
        case TxParameterID::Fee:
        case TxParameterID::MinHeight:
        case TxParameterID::PeerID:
        case TxParameterID::MyID:
        case TxParameterID::CreateTime:
        case TxParameterID::IsSender:
        case TxParameterID::Status:
        case TxParameterID::TransactionType:
        case TxParameterID::KernelID:
        case TxParameterID::AssetID:
            return true;
        default:
            return false;
        }
    }
}
