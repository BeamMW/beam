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

            Height responseHeight = m_Tx.GetMandatoryParameter<Height>(TxParameterID::PeerResponseHeight);
            m_Tx.SetParameter(TxParameterID::PeerResponseHeight, responseHeight, m_SubTxID);
        }
    }

    void LockTxBuilder::LoadPeerOffset()
    {
        m_Tx.GetParameter(TxParameterID::PeerOffset, m_PeerOffset, m_SubTxID);
    }

    ECC::RangeProof::Confidential::Part2 LockTxBuilder::GetRangeProofInitialPart2() const
    {
        ECC::RangeProof::Confidential::Part2 part2;
        ZeroObject(part2);
        ECC::RangeProof::Confidential::MultiSig::CoSignPart(GetSharedSeed(), part2);
        return part2;
    }

    bool LockTxBuilder::CreateSharedUTXOProofPart2(bool isBeamOwner)
    {
        // load peer part2
        m_SharedProof.m_Part2 = m_Tx.GetMandatoryParameter<RangeProof::Confidential::Part2>(TxParameterID::PeerSharedBulletProofPart2, m_SubTxID);

        Output outp;
        outp.m_Commitment = GetSharedCommitment();

        Height minHeight = 0;
        m_Tx.GetParameter(TxParameterID::MinHeight, minHeight, m_SubTxID);

        Oracle oracle;
        outp.Prepare(oracle, minHeight);

        // produce multisig
        if (!m_SharedProof.CoSign(GetSharedSeed(), GetSharedBlindingFactor(), GetProofCreatorParams(isBeamOwner), oracle, RangeProof::Confidential::Phase::Step2))
        {
            return false;
        }

        m_Tx.SetParameter(TxParameterID::SharedBulletProof, m_SharedProof, m_SubTxID);

        return true;
    }

    bool LockTxBuilder::CreateSharedUTXOProofPart3(bool isBeamOwner)
    {
        Output outp;
        outp.m_Commitment = GetSharedCommitment();

        Height minHeight = 0;
        m_Tx.GetParameter(TxParameterID::MinHeight, minHeight, m_SubTxID);

        Oracle oracle;
        outp.Prepare(oracle, minHeight);

        if (isBeamOwner)
        {
            // load peer part3
            m_SharedProof.m_Part3 = m_Tx.GetMandatoryParameter<RangeProof::Confidential::Part3>(TxParameterID::PeerSharedBulletProofPart3, m_SubTxID);

            // finalize proof
            if (!m_SharedProof.CoSign(GetSharedSeed(), GetSharedBlindingFactor(), GetProofCreatorParams(isBeamOwner), oracle, RangeProof::Confidential::Phase::Finalize))
            {
                return false;
            }
        }
        else
        {
            ECC::RangeProof::Confidential::MultiSig msig;
            msig.m_Part1 = m_SharedProof.m_Part1;
            msig.m_Part2 = m_SharedProof.m_Part2;

            ZeroObject(m_SharedProof.m_Part3);
            msig.CoSignPart(GetSharedSeed(), GetSharedBlindingFactor(), oracle, m_SharedProof.m_Part3);
        }

        m_Tx.SetParameter(TxParameterID::SharedBulletProof, m_SharedProof, m_SubTxID);
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

            m_OutputCoins.push_back(m_SharedCoin.m_ID);
            m_Tx.SetParameter(TxParameterID::OutputCoins, m_OutputCoins, m_SubTxID);
            m_Tx.SetParameter(TxParameterID::Outputs, m_Outputs, false, m_SubTxID);

            CoinIDList sharedInputs;
            sharedInputs.push_back(m_SharedCoin.m_ID);
            m_Tx.SetParameter(TxParameterID::InputCoins, sharedInputs, static_cast<SubTxID>(SubTxIndex::BEAM_REDEEM_TX));
            m_Tx.SetParameter(TxParameterID::InputCoins, sharedInputs, static_cast<SubTxID>(SubTxIndex::BEAM_REFUND_TX));

            // blindingFactor = sk + sk1
            beam::SwitchCommitment switchCommitment;
            switchCommitment.Create(m_SharedBlindingFactor, *m_Tx.GetWalletDB()->get_ChildKdf(m_SharedCoin.m_ID), m_SharedCoin.m_ID);
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

    ECC::Point::Native LockTxBuilder::GetPublicExcess() const
    {
        // create shared commitment
        Point::Native pt = GetPublicSharedBlindingFactor();
        AmountBig::AddTo(pt, GetAmount());
        pt = -pt;
        return BaseTxBuilder::GetPublicExcess() + pt;
    }

    const ECC::RangeProof::CreatorParams& LockTxBuilder::GetProofCreatorParams(bool isBeamOwner)
    {
        if (!m_CreatorParams.is_initialized())
        {
            ECC::RangeProof::CreatorParams creatorParams;
            creatorParams.m_Kidv = Zero;
            creatorParams.m_Kidv.m_Value = m_SharedCoin.m_ID.m_Value;

            auto publicSharedBlindingFactor = GetPublicSharedBlindingFactor();
            auto peerPublicSharedBlindingFactor = m_Tx.GetMandatoryParameter<Point::Native>(TxParameterID::PeerPublicSharedBlindingFactor, m_SubTxID);

            // Hash(A_commitment | B_commitment)
            ECC::Hash::Processor()
                << (isBeamOwner ? publicSharedBlindingFactor : peerPublicSharedBlindingFactor)
                << (isBeamOwner ? peerPublicSharedBlindingFactor : publicSharedBlindingFactor)
                >> creatorParams.m_Seed.V;

            m_CreatorParams = creatorParams;
        }
        return m_CreatorParams.get();
    }

    ECC::Point::Native LockTxBuilder::GetSharedCommitment()
    {
        Point::Native commitment(Zero);

        Tag::AddValue(commitment, nullptr, GetAmount());
        commitment += GetPublicSharedBlindingFactor();
        commitment += m_Tx.GetMandatoryParameter<Point::Native>(TxParameterID::PeerPublicSharedBlindingFactor, m_SubTxID);

        return commitment;
    }
}