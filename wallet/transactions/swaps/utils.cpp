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

#include "utils.h"

namespace beam::wallet
{
    const char* getSwapTxStatus(AtomicSwapTransaction::State state)
    {
        static const char* Initial = "waiting for peer";
        static const char* BuildingBeamLockTX = "building Beam LockTX";
        static const char* BuildingBeamRefundTX = "building Beam RefundTX";
        static const char* BuildingBeamRedeemTX = "building Beam RedeemTX";
        static const char* HandlingContractTX = "handling LockTX";
        static const char* SendingRefundTX = "sending RefundTX";
        static const char* SendingRedeemTX = "sending RedeemTX";
        static const char* SendingBeamLockTX = "sending Beam LockTX";
        static const char* SendingBeamRefundTX = "sending Beam RefundTX";
        static const char* SendingBeamRedeemTX = "sending Beam RedeemTX";
        static const char* Completed = "completed";
        static const char* Canceled = "cancelled";
        static const char* Aborted = "aborted";
        static const char* Failed = "failed";

        switch (state)
        {
        case wallet::AtomicSwapTransaction::State::Initial:
            return Initial;
        case wallet::AtomicSwapTransaction::State::BuildingBeamLockTX:
            return BuildingBeamLockTX;
        case wallet::AtomicSwapTransaction::State::BuildingBeamRefundTX:
            return BuildingBeamRefundTX;
        case wallet::AtomicSwapTransaction::State::BuildingBeamRedeemTX:
            return BuildingBeamRedeemTX;
        case wallet::AtomicSwapTransaction::State::HandlingContractTX:
            return HandlingContractTX;
        case wallet::AtomicSwapTransaction::State::SendingRefundTX:
            return SendingRefundTX;
        case wallet::AtomicSwapTransaction::State::SendingRedeemTX:
            return SendingRedeemTX;
        case wallet::AtomicSwapTransaction::State::SendingBeamLockTX:
            return SendingBeamLockTX;
        case wallet::AtomicSwapTransaction::State::SendingBeamRefundTX:
            return SendingBeamRefundTX;
        case wallet::AtomicSwapTransaction::State::SendingBeamRedeemTX:
            return SendingBeamRedeemTX;
        case wallet::AtomicSwapTransaction::State::CompleteSwap:
            return Completed;
        case wallet::AtomicSwapTransaction::State::Canceled:
            return Canceled;
        case wallet::AtomicSwapTransaction::State::Refunded:
            return Aborted;
        case wallet::AtomicSwapTransaction::State::Failed:
            return Failed;
        default:
            assert(false && "Unexpected status");
        }

        return "";
    }

    /// Swap Parameters 
    TxParameters InitNewSwap(const WalletID& myID, Height minHeight, Amount amount, Amount fee, AtomicSwapCoin swapCoin,
        Amount swapAmount, Amount swapFee, bool isBeamSide /*= true*/,
        Height lifetime /*= kDefaultTxLifetime*/, Height responseTime/* = kDefaultTxResponseTime*/)
    {
        TxParameters parameters(GenerateTxID());

        parameters.SetParameter(TxParameterID::TransactionType, TxType::AtomicSwap);
        parameters.SetParameter(TxParameterID::CreateTime, getTimestamp());
        parameters.SetParameter(TxParameterID::Amount, amount);
        if (isBeamSide)
        {
            parameters.SetParameter(TxParameterID::Fee, fee, SubTxIndex::BEAM_LOCK_TX);
            parameters.SetParameter(TxParameterID::Fee, fee, SubTxIndex::BEAM_REFUND_TX);
            parameters.SetParameter(TxParameterID::Fee, swapFee, SubTxIndex::REDEEM_TX);
        }
        else
        {
            parameters.SetParameter(TxParameterID::Fee, fee, SubTxIndex::BEAM_REDEEM_TX);
            parameters.SetParameter(TxParameterID::Fee, swapFee, SubTxIndex::LOCK_TX);
            parameters.SetParameter(TxParameterID::Fee, swapFee, SubTxIndex::REFUND_TX);
        }
        parameters.SetParameter(TxParameterID::Lifetime, lifetime);

        parameters.SetParameter(TxParameterID::MinHeight, minHeight);
        parameters.SetParameter(TxParameterID::PeerResponseTime, responseTime);
        parameters.SetParameter(TxParameterID::MyID, myID);
        parameters.SetParameter(TxParameterID::IsSender, isBeamSide);
        parameters.SetParameter(TxParameterID::IsInitiator, false);

        parameters.SetParameter(TxParameterID::AtomicSwapCoin, swapCoin);
        parameters.SetParameter(TxParameterID::AtomicSwapAmount, swapAmount);
        parameters.SetParameter(TxParameterID::AtomicSwapIsBeamSide, isBeamSide);

        return parameters;
    }

    TxParameters CreateSwapParameters()
    {
        return CreateTransactionParameters(TxType::AtomicSwap, GenerateTxID())
            .SetParameter(TxParameterID::IsInitiator, false);
    }
} // namespace beam::wallet
