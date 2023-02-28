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
#include "dex_tx.h"
#include "wallet/core/wallet.h"
#include "dex_tx_builder.h"

namespace beam::wallet
{
    TxParameters CreateDexTransactionParams(
            const DexOrderID& dexOrderID,
            const WalletID& peerID,
            const WalletID& myID,
            Asset::ID coinMy,
            Amount amountMy,
            Asset::ID coinPeer,
            Amount amountPeer,
            Amount fee,
            const boost::optional<TxID>& txId)
    {
        return CreateTransactionParameters(TxType::DexSimpleSwap, txId)
            .SetParameter(TxParameterID::PeerAddr, peerID)
            .SetParameter(TxParameterID::MyAddr, myID)
            .SetParameter(TxParameterID::DexOrderID, dexOrderID)
            .SetParameter(TxParameterID::SavePeerAddress, false)
            .SetParameter(TxParameterID::DexReceiveAsset, coinPeer)
            .SetParameter(TxParameterID::DexReceiveAmount, amountPeer)
            .SetParameter(TxParameterID::Amount, amountMy)
            .SetParameter(TxParameterID::AssetID, coinMy)
            .SetParameter(TxParameterID::Fee, fee);
    }

    DexTransaction::Creator::Creator(IWalletDB::Ptr wdb)
        : _wdb(std::move(wdb))
    {
    }

    BaseTransaction::Ptr DexTransaction::Creator::Create(const TxContext& context)
    {
        return BaseTransaction::Ptr(new DexTransaction(context));
    }

    TxParameters DexTransaction::Creator::CheckAndCompleteParameters(const TxParameters& initialParams)
    {
        auto params = ProcessReceiverAddress(initialParams, _wdb);

        const auto selfTx = params.GetParameter<bool>(TxParameterID::IsSelfTx);
        if (!selfTx)
        {
            throw InvalidTransactionParametersException("DexSimpleSwap missing IsSelfTx");
        }

        if (*selfTx)
        {
            throw InvalidTransactionParametersException("DexSimpleSwap transaction cannot be sent to the self");
        }

        // TODO: check if set correctly for OUR & External transactions
        //       Should not come from external source but set to order found in locally kept orders
        const auto orderId = params.GetParameter<DexOrderID>(TxParameterID::DexOrderID);
        if (!orderId)
        {
            throw InvalidTransactionParametersException("DexSimpleSwap missing order id");
        }

        //
        // Check Peer
        //
        const auto peerID = params.GetParameter<WalletID>(TxParameterID::PeerAddr);
        if(!peerID)
        {
            throw InvalidTransactionParametersException("DexSimpleSwap missing PeerID");
        }

        const auto peeraddr = _wdb->getAddress(*peerID);
        if (peeraddr)
        {
            throw InvalidTransactionParametersException("DexSimpleSwap transaction should not save peer adddress");
        }

        //
        // Check self
        //
        const auto myID = params.GetParameter<WalletID>(TxParameterID::MyAddr);
        if (!myID.is_initialized())
        {
            throw InvalidTransactionParametersException("DexSimpleSwap missing MyID");
        }

        auto myaddr = _wdb->getAddress(*myID);
        if (myaddr && myaddr->isOwn())
        {
            if(!myaddr->isOwn())
            {
                throw InvalidTransactionParametersException("DexSimpleSwap not own address in MyID");
            }

            params.SetParameter(TxParameterID::MyAddressID, myaddr->m_OwnID);
        }

        //
        // Check assets
        //
        const auto sendID = params.GetParameter<beam::Asset::ID>(TxParameterID::AssetID);
        if (!sendID)
        {
            throw InvalidTransactionParametersException("DexSimpleSwap missing AssetID");
        }

        const auto recevieID = params.GetParameter<beam::Asset::ID>(TxParameterID::DexReceiveAsset);
        if (!recevieID)
        {
            throw InvalidTransactionParametersException("DexSimpleSwap missing DexReceiveAsset");
        }

        if (*recevieID == *sendID)
        {
             throw InvalidTransactionParametersException("DexSimpleSwap same asset on both sides");
        }

        return params;
    }

    DexTransaction::DexTransaction(const TxContext& context)
        : BaseTransaction (TxType::DexSimpleSwap, context)
    {
    }

    bool DexTransaction::IsInSafety() const
    {
        const auto state = GetState<State>();
        return state >= State::KernelConfirmation;
    }

    bool DexTransaction::IsTxParameterExternalSettable(TxParameterID paramID, SubTxID subTxID) const
    {
        if (kDefaultSubTxID != subTxID)
        {
            return false;
        }

        switch (paramID)
        {
        case TxParameterID::IsSender: // TODO:DEX - change this. It should be checked and set by Creator during construction
        case TxParameterID::Amount:
        case TxParameterID::AssetID:
        case TxParameterID::DexReceiveAmount:
        case TxParameterID::DexReceiveAsset:
        case TxParameterID::ExternalDexOrderID:
        case TxParameterID::Fee:
        case TxParameterID::MinHeight: // TODO:DEX check where set
        case TxParameterID::MaxHeight: // TODO:DEX check where set
        case TxParameterID::Message:
        case TxParameterID::Lifetime:  // TODO:DEX check where set
        case TxParameterID::PaymentConfirmation:
        case TxParameterID::PeerProtoVersion:
        case TxParameterID::PeerMaxHeight:
        case TxParameterID::PeerPublicExcess:
        case TxParameterID::PeerPublicNonce:
        case TxParameterID::PeerSignature:
        case TxParameterID::PeerInputs:
        case TxParameterID::PeerOutputs:
        case TxParameterID::PeerOffset:
        case TxParameterID::FailureReason: // to be able to cancel transaction until we haven't sent any response
            return true;
        default:
            return false;
        }
    }

    void DexTransaction::UpdateImpl()
    {
        if (!_builder)
        {
            _builder = std::make_shared<DexSimpleSwapBuilder>(*this);
        }

        if (_builder->m_Coins.IsEmpty())
        {
            _builder->VerifyAssetsEnabled();

            if (_builder->m_AssetID == _builder->m_ReceiveAssetID)
            {
                // TODO:DEX remove, it is done in Check and complete tx
                throw std::runtime_error("same asset on both sides");
            }

            if (_builder->m_AssetID != Asset::s_BeamID)
            {
                if (CheckAsset(_builder->m_AssetID) != AssetCheckResult::OK)
                {
                    // can be request for async operation or simple fail
                    // in both cases we should jump out of here
                    return;
                }
            }

            if (_builder->m_ReceiveAssetID != Asset::s_BeamID)
            {
                if (CheckAsset(_builder->m_ReceiveAssetID) != AssetCheckResult::OK)
                {
                    // can be request for async operation or simple fail
                    // in both cases we should jump out of here
                    return;
                }
            }

            UpdateTxDescription(TxStatus::InProgress);

            std::stringstream ss;
            ss << GetTxID()
               << " Sending via DEX "
               << PrintableAmount(_builder->m_Amount, false, _builder->m_AssetID)
               << " and Receiving "
               << PrintableAmount(_builder->m_ReceiveAmount, false, _builder->m_ReceiveAssetID);

            ss << " (fee: " << PrintableAmount(_builder->m_Fee) << ")";
            LOG_INFO() << ss.str();

            BaseTxBuilder::Balance bb(*_builder);
            bb.m_Map[_builder->m_AssetID].m_Value -= _builder->m_Amount;
            bb.m_Map[_builder->m_ReceiveAssetID].m_Value += _builder->m_ReceiveAmount;
            bb.CreateOutput(_builder->m_ReceiveAmount, _builder->m_ReceiveAssetID, Key::Type::Regular);

            if (_builder->m_IsSender)
            {
                bb.m_Map[Asset::s_BeamID].m_Value -= _builder->m_Fee;
                Height maxResponseHeight = 0;
                if (GetParameter(TxParameterID::PeerResponseHeight, maxResponseHeight))
                {
                    LOG_INFO() << GetTxID() << " Max height for response: " << maxResponseHeight;
                }
            }

            bb.CompleteBalance();

            //
            // Sender always pays all fees
            //
            if (_builder->m_IsSender)
            {
                TxStats tsExtra;
                tsExtra.m_Outputs = 1; // peer always adds only 1 output
                _builder->CheckMinimumFee(&tsExtra);
            }

            _builder->SaveCoins();
        }

        if (!_builder->SignTx())
        {
            return;
        }

        assert(_builder->m_pKrn);
        if (_builder->m_IsSender)
        {
            uint8_t nRegistered = proto::TxStatus::Unspecified;
            if (!GetParameter(TxParameterID::TransactionRegistered, nRegistered))
            {
                if (CheckExpired())
                    return;

                _builder->FinalyzeTx();

                GetGateway().register_tx(GetTxID(), _builder->m_pTransaction);
                SetState(State::Registration);
                return;
            }

            if (proto::TxStatus::Ok != nRegistered)
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
            ConfirmKernel(_builder->m_pKrn->m_Internal.m_ID);
            return;
        }

        SetCompletedTxCoinStatuses(hProof);
        CompleteTx();
    }
}
