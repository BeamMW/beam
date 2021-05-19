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

namespace beam::wallet
{
    TxParameters CreateDexTransactionParams(const WalletID& peerID, const DexOrderID& dexOrderID, const boost::optional<TxID>& txId)
    {
        return CreateTransactionParameters(TxType::DexSimpleSwap, txId)
            .SetParameter(TxParameterID::PeerID, peerID)
            .SetParameter(TxParameterID::DexOrderID, dexOrderID);
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
        CheckSenderAddress(initialParams, _wdb);
        auto params = ProcessReceiverAddress(initialParams, _wdb, true);

        const auto orderId = params.GetParameter<DexOrderID>(TxParameterID::DexOrderID);
        if (!orderId)
        {
            throw InvalidTransactionParametersException("Missing order id");
        }

        const auto selfTx = params.GetParameter<bool>(TxParameterID::IsSelfTx);
        if (!selfTx)
        {
            throw InvalidTransactionParametersException("Missing IsSelfTx");
        }

        if (*selfTx)
        {
            throw InvalidTransactionParametersException("DexSimpleSwap transaction cannot be sent to the self");
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
            return false; // irrelevant

        switch (paramID)
        {
        case TxParameterID::IsSender: // TODO:DEX - change this. It should be checked and set by Creator during construction
        case TxParameterID::Amount:
        case TxParameterID::AssetID:
        case TxParameterID::Fee:
        case TxParameterID::MinHeight:
        case TxParameterID::MaxHeight:
        case TxParameterID::Message:
        case TxParameterID::Lifetime:
        case TxParameterID::PaymentConfirmation:
        case TxParameterID::PeerProtoVersion:
        case TxParameterID::PeerWalletIdentity:
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
         CompleteTx();
    }
}
