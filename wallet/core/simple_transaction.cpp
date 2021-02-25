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
#include "wallet/core/common_utils.h"

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

    BaseTransaction::Ptr SimpleTransaction::Creator::Create(const TxContext& context)
    {
        return BaseTransaction::Ptr(new SimpleTransaction(context));
    }

    TxParameters SimpleTransaction::Creator::CheckAndCompleteParameters(const TxParameters& parameters)
    {
        CheckSenderAddress(parameters, m_WalletDB);
        return ProcessReceiverAddress(parameters, m_WalletDB);
    }

    SimpleTransaction::SimpleTransaction(const TxContext& context)
        : BaseTransaction (TxType::Simple, context)
    {
    }

    bool SimpleTransaction::IsInSafety() const
    {
        const auto txState = GetState<State>();
        return txState == State::KernelConfirmation;
    }

    struct SimpleTransaction::MyBuilder
        :public MutualTxBuilder
    {
        using MutualTxBuilder::MutualTxBuilder;
        void SendToPeer(SetTxParameter&&) override;
    };

    void SimpleTransaction::MyBuilder::SendToPeer(SetTxParameter&& msg)
    {
        Height hMax = (m_Height.m_Max != MaxHeight) ?
            m_Height.m_Max :
            m_Height.m_Min + m_Lifetime;

        // common parameters
        msg
            .AddParameter(TxParameterID::PeerProtoVersion, s_ProtoVersion)
            .AddParameter(TxParameterID::PeerMaxHeight, hMax);

        if (m_IsSender)
        {
            msg
                .AddParameter(TxParameterID::Amount, m_Amount)
                .AddParameter(TxParameterID::AssetID, m_AssetID)
                .AddParameter(TxParameterID::Fee, m_Fee)
                .AddParameter(TxParameterID::MinHeight, m_Height.m_Min)
                .AddParameter(TxParameterID::Lifetime, m_Lifetime)
                .AddParameter(TxParameterID::IsSender, false);

        }
        else
        {
            LOG_INFO() << m_Tx.GetTxID() << " Transaction accepted. Kernel: " << GetKernelIDString();

            Signature sig;
            if (!m_Tx.GetParameter(TxParameterID::PaymentConfirmation, sig, m_SubTxID))
            {
                // legacy peer
                // Provide older-style payment confirmation, signed by the sbbs addr
                PaymentConfirmation pc;
                WalletID widPeer, widMy;
                bool bSuccess =
                    m_Tx.GetParameter(TxParameterID::PeerID, widPeer, m_SubTxID) &&
                    m_Tx.GetParameter(TxParameterID::MyID, widMy, m_SubTxID);

                if (bSuccess)
                {
                    pc.m_Sender = widPeer.m_Pk;
                    pc.m_Value = m_Amount;
                    pc.m_AssetID = m_AssetID;
                    pc.m_KernelID = m_pKrn->m_Internal.m_ID;

                    auto waddr = m_Tx.GetWalletDB()->getAddress(widMy);
                    if (waddr && waddr->isOwn())
                    {
                        Scalar::Native sk;
                        m_Tx.GetWalletDB()->get_SbbsPeerID(sk, widMy.m_Pk, waddr->m_OwnID);

                        pc.Sign(sk);
                        msg.AddParameter(TxParameterID::PaymentConfirmation, pc.m_Signature);
                    }
                }
            }
        }

        m_Tx.SendTxParametersStrict(move(msg));
    }

    bool SimpleTransaction::IsTxParameterExternalSettable(TxParameterID paramID, SubTxID subTxID) const
    {
        if (kDefaultSubTxID != subTxID)
            return false; // irrelevant

        switch (paramID)
        {
        case TxParameterID::IsSender: // TODO - change this. It should be checked and set by Creator during construction
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

    void SimpleTransaction::UpdateImpl()
    {
        if (!m_TxBuilder)
        {
            m_TxBuilder = IsSelfTx() ?
                make_shared<SimpleTxBuilder>(*this, kDefaultSubTxID) :
                make_shared<MyBuilder>(*this, kDefaultSubTxID);
        }

        auto sharedBuilder = m_TxBuilder; // extra ref?

        SimpleTxBuilder& builder = *m_TxBuilder;
        MyBuilder* pMutualBuilder = IsSelfTx() ? nullptr : &Cast::Up<MyBuilder>(builder);

        if (builder.m_Coins.IsEmpty())
        {
            if (builder.m_AssetID)
            {
                builder.VerifyAssetsEnabled();

                if (CheckAsset(builder.m_AssetID) != AssetCheckResult::OK)
                    // can be request for async operation or simple fail
                    // in both cases we should jump out of here
                    return;
            }

            // we are at the beginning
            uint64_t nAddrOwnID;
            if (!GetParameter(TxParameterID::MyAddressID, nAddrOwnID))
            {
                WalletID wid;
                if (GetParameter(TxParameterID::MyID, wid))
                {
                    auto waddr = GetWalletDB()->getAddress(wid);
                    if (waddr && waddr->isOwn())
                    {
                        SetParameter(TxParameterID::MyAddressID, waddr->m_OwnID);
                        SetParameter(TxParameterID::MyWalletIdentity, waddr->m_Identity);
                    }
                }
            }

            PeerID myWalletID, peerWalletID;
            bool hasID = GetParameter<PeerID>(TxParameterID::MyWalletIdentity, myWalletID)
                && GetParameter<PeerID>(TxParameterID::PeerWalletIdentity, peerWalletID);

            UpdateTxDescription(TxStatus::InProgress);

            if (!pMutualBuilder && (MaxHeight == builder.m_Height.m_Max) && builder.m_Lifetime)
            {
                // for split tx we finalyze the max height immediately
                Block::SystemState::Full s;
                if (GetTip(s))
                {
                    builder.m_Height.m_Max = s.m_Height + builder.m_Lifetime;
                    SetParameter(TxParameterID::MaxHeight, builder.m_Height.m_Max, GetSubTxID());
                }
            }

            BaseTxBuilder::Balance bb(builder);

            if (pMutualBuilder && pMutualBuilder->m_IsSender)
            {
                // snd
                bb.m_Map[builder.m_AssetID].m_Value -= builder.m_Amount;

                Height maxResponseHeight = 0;
                if (GetParameter(TxParameterID::PeerResponseHeight, maxResponseHeight)) {
                    LOG_INFO() << GetTxID() << " Max height for response: " << maxResponseHeight;
                }
            }
            else
            {
                // rcv or split. Create TXOs explicitly
                AmountList amoutList;
                if (!GetParameter(TxParameterID::AmountList, amoutList))
                    amoutList = AmountList{ builder.m_Amount };

                // rcv or split: create tx output utxo(s)
                for (const auto& amount : amoutList)
                    bb.CreateOutput(amount, builder.m_AssetID, Key::Type::Regular);
            }

            bool bPayFee = !pMutualBuilder || pMutualBuilder->m_IsSender;
            if (bPayFee)
                bb.m_Map[0].m_Value -= builder.m_Fee;
            else
                // rcv
                bb.m_Map[builder.m_AssetID].m_Value += builder.m_Amount;

            bb.CompleteBalance();

            if (bPayFee)
            {
                // snd or split. Take care of fee
                TxStats tsExtra;
                if (pMutualBuilder)
                    tsExtra.m_Outputs = 1; // normally peer adds 1 output

                // split or sender, pay the fee
                builder.CheckMinimumFee(&tsExtra);
            }

            stringstream ss;
            ss << GetTxID() << (pMutualBuilder ? pMutualBuilder->m_IsSender ? " Sending " : " Receiving " : " Splitting ")
                << PrintableAmount(builder.m_Amount, false, builder.m_AssetID ? kAmountASSET : "", builder.m_AssetID ? kAmountAGROTH : "")
                << " (fee: " << PrintableAmount(builder.m_Fee) << ")";

            if (builder.m_AssetID)
                ss << ", asset ID: " << builder.m_AssetID;

            if (hasID)
                ss << ", my ID: " << myWalletID << ", peer ID: " << peerWalletID;

            LOG_INFO() << ss.str();

            builder.SaveCoins();
        }

        if (!builder.SignTx())
            return;

        assert(builder.m_pKrn);

        if (!pMutualBuilder || pMutualBuilder->m_IsSender)
        {
            // We're the tx owner
            uint8_t nRegistered = proto::TxStatus::Unspecified;
            if (!GetParameter(TxParameterID::TransactionRegistered, nRegistered))
            {
                if (CheckExpired())
                    return;

                builder.FinalyzeTx();

                GetGateway().register_tx(GetTxID(), builder.m_pTransaction);
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
        }

        Height hProof = 0;
        GetParameter(TxParameterID::KernelProofHeight, hProof);
        if (!hProof)
        {
            SetState(State::KernelConfirmation);
            ConfirmKernel(builder.m_pKrn->m_Internal.m_ID);
            return;
        }

        SetCompletedTxCoinStatuses(hProof);
        CompleteTx();
    }
}
