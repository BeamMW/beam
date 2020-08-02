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
        :MutualTxBuilder2(tx, subTxID)
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

        if (!m_IsSender && (Status::RcvFullHalfSig == m_Status) && IsRedeem())
        {
            // remove the partial signature from the response!
            size_t iDst = 0;
            for (size_t iSrc = 0; iSrc < msgSub.m_Parameters.size(); iSrc++)
            {
                auto& src = msgSub.m_Parameters[iSrc];
                if (TxParameterID::PeerSignature == src.first)
                    continue;

                if (iDst != iSrc)
                    msgSub.m_Parameters[iDst] = std::move(src);
                iDst++;
            }

            msgSub.m_Parameters.resize(iDst);
        }

        std::move(msgSub.m_Parameters.begin(), msgSub.m_Parameters.end(), std::back_inserter(msg.m_Parameters));
        m_Tx.SendTxParametersStrict(std::move(msg));
    }

    bool SharedTxBuilder::SignTxSender()
    {
        if (!MutualTxBuilder2::SignTxSender())
            return false;
        if (!IsRedeem())
            return true;

        switch (m_Status)
        {
        case Status::SndFull: // not really full, rcv sig is missing
        {
            ECC::Scalar::Native kSig = m_pKrn->m_Signature.m_k; // my partial


            // Send BTC side partial sign with secret
            NoLeak<Scalar> kSecret;
            GetParameterStrict(TxParameterID::AtomicSwapSecretPrivateKey, kSecret.V);
            kSig += kSecret.V;
            kSecret.V = kSig;

            SetTxParameter msg;
            msg.AddParameter(TxParameterID::PeerSignature, kSecret.V);

            SendToPeer(std::move(msg));

            SetStatus(Status::SndSigSent);

        }
        // no break;

        case Status::SndSigSent:
        {
            ECC::Scalar k;
            if (!GetParameter(TxParameterID::PeerSignature, k))
                break;

            ECC::Scalar::Native kSig = m_pKrn->m_Signature.m_k;
            kSig += k;
            m_pKrn->m_Signature.m_k = kSig;

            SetStatus(Status::SndFull2);
        }
        }

        return (m_Status >= Status::SndFull2) && !IsGeneratingInOuts();
    }

    bool SharedTxBuilder::SignTxReceiver()
    {
        if (!MutualTxBuilder2::SignTxReceiver())
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
                m_pKrn->m_Signature.get_Challenge(e, m_pKrn->m_Internal.m_ID);

                // k*G + e*Nonce + 

                ptNonce += ptExc * e;
                ptNonce += Context::get().G * k;

                if (ptNonce == Zero)
                    throw TransactionFailedException(true, TxFailureReason::FailedToCreateMultiSig); // secret pubkey is missing

                Point ptPubKey;
                ptNonce.Export(ptPubKey);

                SetParameter(TxParameterID::AtomicSwapSecretPublicKey, ptPubKey);

                SetTxParameter msg;
                msg.AddParameter(TxParameterID::PeerSignature, m_pKrn->m_Signature.m_k);
                SendToPeer(std::move(msg));

                // save the aggregate signature (later our kernel will be overwritten)
                ECC::Scalar::Native sk(k);
                sk += m_pKrn->m_Signature.m_k;
                k = sk;

                SetParameter(TxParameterID::AggregateSignature, k);

                SetStatus(Status::RcvSigSent2);
            }
            // no break;
        }

        return (m_Status >= Status::RcvSigSent2);
    }

    void SharedTxBuilder::FinalyzeTxInternal()
    {
        assert(m_IsSender);

        // don't call parent method, it will add peer's ins/outs, this must be prevented
        AddPeerOffset();
        SimpleTxBuilder::FinalyzeTxInternal();
    }

    void SharedTxBuilder::AddPeerSignature(const ECC::Point::Native& ptNonce, const ECC::Point::Native& ptExc)
    {
        if (!IsRedeem()) // for redeem it's omitted at this stage
            MutualTxBuilder2::AddPeerSignature(ptNonce, ptExc);
    }


}
