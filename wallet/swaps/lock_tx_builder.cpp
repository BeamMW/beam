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
    LockTxBuilder::LockTxBuilder(BaseTransaction& tx, Amount amount, Amount fee)
        : BaseTxBuilder(tx, SubTxIndex::BEAM_LOCK_TX, { amount }, fee)
    {
        Height minHeight = 0;
        if (!m_Tx.GetParameter(TxParameterID::MinHeight, minHeight, m_SubTxID))
        {
            // Get MinHeight from main TX
            minHeight = m_Tx.GetMandatoryParameter<Height>(TxParameterID::MinHeight);
            m_Tx.SetParameter(TxParameterID::MinHeight, minHeight, m_SubTxID);

            Height lifetime = m_Tx.GetMandatoryParameter<Height>(TxParameterID::Lifetime);
            m_Tx.SetParameter(TxParameterID::Lifetime, lifetime, m_SubTxID);

            if (m_Tx.IsInitiator())
            {
                Height responseTime = m_Tx.GetMandatoryParameter<Height>(TxParameterID::PeerResponseHeight);
                m_Tx.SetParameter(TxParameterID::PeerResponseHeight, responseTime, m_SubTxID);
            }
        }
    }

    void LockTxBuilder::LoadPeerOffset()
    {
        m_Tx.GetParameter(TxParameterID::PeerOffset, m_PeerOffset, m_SubTxID);
    }

    bool LockTxBuilder::SharedUTXOProofPart2(bool shouldProduceMultisig)
    {
        if (shouldProduceMultisig)
        {
            // load peer part2
            if (!m_Tx.GetParameter(TxParameterID::PeerSharedBulletProofPart2, m_SharedProof.m_Part2, m_SubTxID))
            {
                return false;
            }

			Output outp;
			outp.m_Commitment = GetSharedCommitment();

			Height minHeight = 0;
			m_Tx.GetParameter(TxParameterID::MinHeight, minHeight, m_SubTxID);

            Oracle oracle;
			outp.Prepare(oracle, minHeight);

            // produce multisig
            m_SharedProof.CoSign(GetSharedSeed(), GetSharedBlindingFactor(), GetProofCreatorParams(), oracle, RangeProof::Confidential::Phase::Step2);

			m_ProofPartialMultiSig.m_Part1 = m_SharedProof.m_Part1;
			m_ProofPartialMultiSig.m_Part2 = m_SharedProof.m_Part2;

            // save SharedBulletProofMSig and BulletProof ?
            m_Tx.SetParameter(TxParameterID::SharedBulletProof, m_SharedProof, m_SubTxID);
        }
        else
        {
            ZeroObject(m_SharedProof.m_Part2);
            RangeProof::Confidential::MultiSig::CoSignPart(GetSharedSeed(), m_SharedProof.m_Part2);
        }
        return true;
    }

    bool LockTxBuilder::SharedUTXOProofPart3(bool shouldProduceMultisig)
    {
		Output outp;
		outp.m_Commitment = GetSharedCommitment();

		Height minHeight = 0;
		m_Tx.GetParameter(TxParameterID::MinHeight, minHeight, m_SubTxID);

		Oracle oracle;
		outp.Prepare(oracle, minHeight);

		if (shouldProduceMultisig)
        {
            // load peer part3
            if (!m_Tx.GetParameter(TxParameterID::PeerSharedBulletProofPart3, m_SharedProof.m_Part3, m_SubTxID))
            {
                return false;
            }

            // finalize proof
            m_SharedProof.CoSign(GetSharedSeed(), GetSharedBlindingFactor(), GetProofCreatorParams(), oracle, RangeProof::Confidential::Phase::Finalize);

            m_Tx.SetParameter(TxParameterID::SharedBulletProof, m_SharedProof, m_SubTxID);
        }
        else
        {
            if (!m_Tx.GetParameter(TxParameterID::PeerSharedBulletProofMSig, m_ProofPartialMultiSig, m_SubTxID))
            {
                return false;
            }

            ZeroObject(m_SharedProof.m_Part3);
            m_ProofPartialMultiSig.CoSignPart(GetSharedSeed(), GetSharedBlindingFactor(), oracle, m_SharedProof.m_Part3);
        }
        return true;
    }

    void LockTxBuilder::AddSharedOutput()
    {
        Output::Ptr output = std::make_unique<Output>();
        output->m_Commitment = GetSharedCommitment();
        output->m_pConfidential = std::make_unique<ECC::RangeProof::Confidential>();
        *(output->m_pConfidential) = m_SharedProof;

        m_Outputs.push_back(std::move(output));
    }

    void LockTxBuilder::LoadSharedParameters()
    {
        if (!m_Tx.GetParameter(TxParameterID::SharedBlindingFactor, m_SharedBlindingFactor, m_SubTxID))
        {
            m_SharedCoin = m_Tx.GetWalletDB()->generateSharedCoin(GetAmount());
            m_Tx.SetParameter(TxParameterID::SharedCoinID, m_SharedCoin.m_ID, m_SubTxID);

            // blindingFactor = sk + sk1
            beam::SwitchCommitment switchCommitment;
            switchCommitment.Create(m_SharedBlindingFactor, *m_Tx.GetWalletDB()->get_ChildKdf(m_SharedCoin.m_ID.m_SubIdx), m_SharedCoin.m_ID);
            m_Tx.SetParameter(TxParameterID::SharedBlindingFactor, m_SharedBlindingFactor, m_SubTxID);

            Oracle oracle;
            RangeProof::Confidential::GenerateSeed(m_SharedSeed.V, m_SharedBlindingFactor, GetAmount(), oracle);
            m_Tx.SetParameter(TxParameterID::SharedSeed, m_SharedSeed.V, m_SubTxID);
        }
        else
        {
            // load remaining shared parameters
            m_Tx.GetParameter(TxParameterID::SharedSeed, m_SharedSeed.V, m_SubTxID);
            m_Tx.GetParameter(TxParameterID::SharedCoinID, m_SharedCoin.m_ID, m_SubTxID);
            m_Tx.GetParameter(TxParameterID::SharedBulletProof, m_SharedProof, m_SubTxID);
        }

        ECC::Scalar::Native blindingFactor = -m_SharedBlindingFactor;
        m_Offset += blindingFactor;
    }

    Transaction::Ptr LockTxBuilder::CreateTransaction()
    {
        AddSharedOutput();
        LoadPeerOffset();
        return BaseTxBuilder::CreateTransaction();
    }

    const ECC::uintBig& LockTxBuilder::GetSharedSeed() const
    {
        return m_SharedSeed.V;
    }

    const ECC::Scalar::Native& LockTxBuilder::GetSharedBlindingFactor() const
    {
        return m_SharedBlindingFactor;
    }

    const ECC::RangeProof::Confidential& LockTxBuilder::GetSharedProof() const
    {
        return m_SharedProof;
    }

    const ECC::RangeProof::Confidential::MultiSig& LockTxBuilder::GetProofPartialMultiSig() const
    {
        return m_ProofPartialMultiSig;
    }

    ECC::Point::Native LockTxBuilder::GetPublicSharedBlindingFactor() const
    {
        return Context::get().G * GetSharedBlindingFactor();
    }

    const ECC::RangeProof::CreatorParams& LockTxBuilder::GetProofCreatorParams()
    {
        if (!m_CreatorParams.is_initialized())
        {
            ECC::RangeProof::CreatorParams creatorParams;
            creatorParams.m_Kidv = m_SharedCoin.m_ID;
            beam::Output::GenerateSeedKid(creatorParams.m_Seed.V, GetSharedCommitment(), *m_Tx.GetWalletDB()->get_MasterKdf());
            m_CreatorParams = creatorParams;
        }
        return m_CreatorParams.get();
    }

    ECC::Point::Native LockTxBuilder::GetSharedCommitment()
    {
        Point::Native commitment(Zero);
        // TODO: check pHGen
        Tag::AddValue(commitment, nullptr, GetAmount());
        commitment += GetPublicSharedBlindingFactor();
        commitment += m_Tx.GetMandatoryParameter<Point::Native>(TxParameterID::PeerPublicSharedBlindingFactor, m_SubTxID);

        return commitment;
    }
}