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


#include "shared_tx_builder.h"

using namespace ECC;

namespace beam::wallet
{

    SharedTxBuilder::SharedTxBuilder(BaseTransaction& tx, SubTxID subTxID)
        :MutualTxBuilder(tx, subTxID)
    {
        m_Lifetime = 0; // disable auto max height adjustment
    }

    bool SharedTxBuilder::AddSharedOffset()
    {
        if (m_pTransaction->m_Offset.m_Value == Zero)
        {
            NoLeak<Scalar> k;
            if (!m_Tx.GetParameter(TxParameterID::SharedBlindingFactor, k.V, SubTxIndex::BEAM_LOCK_TX))
                return false;

            AddOffset(k.V);
        }
        return true;
    }

    bool SharedTxBuilder::AddSharedInput()
    {
        if (m_pTransaction->m_vInputs.empty() && (Status::FullTx != m_Status))
        {
            Input::Ptr pInp(std::make_unique<Input>());
            if (!m_Tx.GetParameter(TxParameterID::SharedCommitment, pInp->m_Commitment, SubTxIndex::BEAM_LOCK_TX))
                return false;

            m_pTransaction->m_vInputs.push_back(std::move(pInp));
        }

        return true;
    }

    void SharedTxBuilder::SendToPeer(SetTxParameter&& msgSub)
    {
        SetTxParameter msg;
        msg.AddParameter(TxParameterID::SubTxIndex, m_SubTxID);

        if (m_IsSender && (Status::SndHalf == m_Status))
        {
            msg
                .AddParameter(TxParameterID::Amount, m_Amount) // legacy
                .AddParameter(TxParameterID::Fee, m_Fee)
                .AddParameter(TxParameterID::MinHeight, m_Height.m_Min); // legacy, for older peers. Current proto automatically deduces it on both sides
        }

        std::move(msgSub.m_Parameters.begin(), msgSub.m_Parameters.end(), std::back_inserter(msg.m_Parameters));
        m_Tx.SendTxParametersStrict(std::move(msg));
    }

    bool SharedTxBuilder::SignTxSender()
    {
        if (!MutualTxBuilder::SignTxSender())
            return false;
        if (!IsRedeem())
            return true;

        switch (m_Status)
        {
            case Status::SndFull:
            {
                ECC::Scalar::Native kSig = m_pKrn->CastTo_Std().m_Signature.m_k; // full sig

                Scalar k;
                GetParameterStrict(TxParameterID::PeerSignature, k);
                kSig -= k; // my partial

                // Send BTC side partial sign with secret
                GetParameterStrict(TxParameterID::AtomicSwapSecretPrivateKey, k);
                kSig += k; // my partial + BTC sign secret
                k = kSig;

                SetTxParameter msg;
                msg.AddParameter(TxParameterID::PeerSignature, k);
                SendToPeer(std::move(msg));

                SetStatus(Status::SndSig2Sent);
            }
        }

        return (m_Status >= Status::SndSig2Sent) && !IsGeneratingInOuts();
    }

    bool SharedTxBuilder::SignTxReceiver()
    {
        if (!MutualTxBuilder::SignTxReceiver())
            return false;
        if (!IsRedeem())
            return true;

        switch (m_Status)
        {
            case Status::RcvFullHalfSigSent:
            {
                ECC::Scalar k;
                if (!GetParameter(TxParameterID::PeerSignature, k))
                    break;

                // recover missing pubkey
                ECC::Point::Native ptNonce, ptExc;
                if (!LoadPeerPart(ptNonce, ptExc))
                    break; // shouldn't happen actually

                Scalar::Native e;
                m_pKrn->CastTo_Std().m_Signature.get_Challenge(e, m_pKrn->m_Internal.m_ID);

                ptNonce += ptExc * e;
                ptNonce += Context::get().G * k;

                if (ptNonce == Zero)
                    throw TransactionFailedException(true, TxFailureReason::FailedToCreateMultiSig); // secret pubkey is missing

                Point ptPubKey;
                ptNonce.Export(ptPubKey);
                SetParameter(TxParameterID::AtomicSwapSecretPublicKey, ptPubKey);

                // save the aggregate signature (later our kernel will be overwritten)
                ECC::Scalar::Native sk(k);
                sk += m_pKrn->CastTo_Std().m_Signature.m_k;
                k = sk;

                SetParameter(TxParameterID::AggregateSignature, k);

                SetStatus(Status::RcvSig2Received);
            }
        }

        return (m_Status >= Status::RcvSig2Received);
    }

    void SharedTxBuilder::FinalyzeTxInternal()
    {
        assert(m_IsSender);

        // don't call parent method, it will add peer's ins/outs, this must be prevented
        AddPeerOffset();
        SimpleTxBuilder::FinalyzeTxInternal();
    }

}
