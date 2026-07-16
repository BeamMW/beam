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

#include "common.h"
#include "lock_tx_builder.h"

using namespace ECC;

namespace beam::wallet
{
    LockTxBuilder::LockTxBuilder(BaseTransaction& tx, Amount amount)
        :MutualTxBuilder(tx, SubTxIndex::BEAM_LOCK_TX)
    {
        m_Amount = amount;
        if (!GetParameter(TxParameterID::SharedBlindingFactor, m_Sk))
        {
            ECC::Hash::Value& tmp = m_SeedSk.V;
            ECC::GenRandom(tmp);
            m_Tx.get_MasterKdfStrict()->DeriveKey(m_Sk, tmp);
            SetParameter(TxParameterID::SharedBlindingFactor, m_Sk);

            AddOffset(-m_Sk);
        }

        m_PubKey.m_Y = 2; // invalid
    }

    void LockTxBuilder::SendToPeer(SetTxParameter&& msgSub)
    {
        // params for parent tx
        SetTxParameter msg;
        msg.AddParameter(TxParameterID::PeerProtoVersion, kSwapProtoVersion);

        if (m_IsSender)
            msg.AddParameter(TxParameterID::AtomicSwapPeerPublicKey, m_Tx.GetMandatoryParameter<std::string>(TxParameterID::AtomicSwapPublicKey));

        // for our subtx
        EvaluateSelfFields();

        // individual Part2
        ECC::RangeProof::Confidential::Part2 p2;
        ZeroObject(p2);
        ECC::RangeProof::Confidential::MultiSig::CoSignPart(m_SeedSk.V, p2);

        msg
            .AddParameter(TxParameterID::SubTxIndex, SubTxIndex::BEAM_LOCK_TX)
            .AddParameter(TxParameterID::PeerPublicSharedBlindingFactor, m_PubKey)
            .AddParameter(TxParameterID::PeerSharedBulletProofPart2, p2);

        if (m_IsSender)
            msg.AddParameter(TxParameterID::Fee, m_Fee);
        else
        {
            Output outp;
            ECC::RangeProof::Confidential proof;
            EvaluateOutput(outp, proof);

            msg.AddParameter(TxParameterID::PeerSharedBulletProofPart3, proof.m_Part3);
        }

        std::move(msgSub.m_Parameters.begin(), msgSub.m_Parameters.end(), std::back_inserter(msg.m_Parameters));
        m_Tx.SendTxParametersStrict(std::move(msg));
    }

    void LockTxBuilder::EvaluateSelfFields()
    {
        if (m_PubKey.m_Y < 2)
            return;

        m_PubKeyN = Context::get().G * m_Sk;
        m_PubKeyN.Export(m_PubKey);

        ECC::Oracle()
            << "swap.bp.seed"
            << m_Sk
            >> m_SeedSk.V;
    }

    void LockTxBuilder::EvaluateOutput(Output& outp, ECC::RangeProof::Confidential& proof)
    {
        EvaluateSelfFields();

        // Shared seed: Hash(A_commitment | B_commitment)
        ECC::RangeProof::Params::Create cp;
        cp.m_Value = m_Amount;

        GetParameterStrict(TxParameterID::PeerPublicSharedBlindingFactor, outp.m_Commitment);

        ECC::Hash::Processor()
            << (m_IsSender ? m_PubKey : outp.m_Commitment)
            << (m_IsSender ? outp.m_Commitment : m_PubKey)
            >> cp.m_Seed.V;

        // asset: both parties derive the same generator blinding from the
        // shared seed. The surjection proof's witness is only this scalar,
        // not the output blinding, so neither party learns anything extra.
        ECC::Point::Native hGen;          // unblinded asset generator
        ECC::Point::Native hGenBlinded;   // hGen + skGen*G, carried by the proof
        const ECC::Point::Native* pGen = nullptr;
        const ECC::Point::Native* pGenBlinded = nullptr;
        ECC::Scalar::Native skSign = m_Sk;

        if (m_AssetID)
        {
            ECC::Hash::Value hv;
            ECC::Hash::Processor()
                << "swap.asset.gen"
                << cp.m_Seed.V
                << m_AssetID
                >> hv;

            ECC::Scalar::Native skGen;
            ECC::NonceGenerator("swap.asset.sk")
                << hv
                >> skGen;

            Asset::Base(m_AssetID).get_Generator(hGen);
            pGen = &hGen;

            outp.m_pAsset = std::make_unique<Asset::Proof>();
            outp.m_pAsset->Create(m_Height.m_Min, hGenBlinded, skGen, m_AssetID, hGen);
            pGenBlinded = &hGenBlinded;

            // the commitment stays v*H_aid + (skA+skB)*G while the rangeproof
            // runs on the blinded generator: the sender's proof share absorbs
            // the v*skGen skew, so ins/outs/offsets balance as usual
            if (m_IsSender)
                Asset::Proof::ModifySk(skSign, skGen, cp.m_Value);
        }

        // commitment
        ECC::Point::Native pt;
        if (!pt.Import(outp.m_Commitment))
            throw TransactionFailedException(true, TxFailureReason::FailedToCreateMultiSig);

        pt += m_PubKeyN;
        Tag::AddValue(pt, pGen, cp.m_Value);

        pt.Export(outp.m_Commitment);

        SetParameter(TxParameterID::SharedCommitment, outp.m_Commitment);

        // proof: Part1, aggregated Part2
        GetParameterStrict(TxParameterID::PeerSharedBulletProofPart2, proof.m_Part2);

        Oracle o1;
        outp.Prepare(o1, m_Height.m_Min);
        Oracle o2(o1);

        uint32_t iVersion = Rules::get().get_BpScheme(m_Height.m_Min);
        if (!proof.CoSign(m_SeedSk.V, skSign, cp, iVersion, o1, RangeProof::Confidential::Phase::Step2, pGenBlinded))
            throw TransactionFailedException(true, TxFailureReason::FailedToCreateMultiSig);

        if (m_IsSender)
        {
            // complete proof:
            GetParameterStrict(TxParameterID::PeerSharedBulletProofPart3, proof.m_Part3);

            if (!proof.CoSign(m_SeedSk.V, skSign, cp, iVersion, o2, RangeProof::Confidential::Phase::Finalize, pGenBlinded))
                throw TransactionFailedException(true, TxFailureReason::FailedToCreateMultiSig);
        }
        else
        {
            // Part3
            RangeProof::Confidential::MultiSig msig;
            msig.m_Part1 = proof.m_Part1;
            msig.m_Part2 = proof.m_Part2;

            ZeroObject(proof.m_Part3);
            msig.CoSignPart(m_SeedSk.V, skSign, o2, proof.m_Part3);
        }
    }

    void LockTxBuilder::FinalizeTxInternal()
    {
        assert(m_IsSender);

        Output::Ptr pOutp = std::make_unique<Output>();
        pOutp->m_pConfidential = std::make_unique<ECC::RangeProof::Confidential>();

        EvaluateOutput(*pOutp, *pOutp->m_pConfidential);

        // check it
        ECC::Point::Native comm;
        if (!pOutp->IsValid(m_Height.m_Min, comm))
            throw TransactionFailedException(true, TxFailureReason::FailedToCreateMultiSig);

        m_pTransaction->m_vOutputs.push_back(std::move(pOutp));

        MutualTxBuilder::FinalizeTxInternal();
    }
}
