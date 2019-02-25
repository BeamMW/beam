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

#include "swap_transaction.h"

using namespace std;
using namespace ECC;

namespace beam::wallet
{
    AtomicSwapTransaction::AtomicSwapTransaction(INegotiatorGateway& gateway
                                               , beam::IWalletDB::Ptr walletDB
                                               , const TxID& txID)
        : BaseTransaction(gateway, walletDB, txID)
    {

    }

    TxType AtomicSwapTransaction::GetType() const
    {
        return TxType::AtomicSwap;
    }

    AtomicSwapTransaction::State AtomicSwapTransaction::GetState(SubTxID subTxID) const
    {
        State state = State::Initial;
        GetParameter(TxParameterID::State, state, subTxID);
        return state;
    }

    void AtomicSwapTransaction::UpdateImpl()
    {
        bool isSender = GetMandatoryParameter<bool>(TxParameterID::IsSender);
        State lockTxState = GetState(SubTxIndex::LOCK_TX);
        Amount amount = GetMandatoryParameter<Amount>(TxParameterID::Amount);

        if (!m_LockTxBuilder)
        {
            m_LockTxBuilder = std::make_unique<LockTxBuilder>(*this, SubTxIndex::LOCK_TX, amount, GetMandatoryParameter<Amount>(TxParameterID::Fee));
        }

        if (!m_LockTxBuilder->GetInitialTxParams() && lockTxState == State::Initial)
        {
            if (CheckExpired())
            {
                return;
            }

            if (isSender)
            {
                m_LockTxBuilder->SelectInputs();
                m_LockTxBuilder->AddChangeOutput();
            }

            if (!m_LockTxBuilder->FinalizeOutputs())
            {
                // TODO: transaction is too big :(
            }

            UpdateTxDescription(TxStatus::InProgress);
        }

        m_LockTxBuilder->CreateKernel();

        if (!m_LockTxBuilder->GetPeerPublicExcessAndNonce())
        {
            assert(IsInitiator());
            if (lockTxState == State::Initial)
            {
                SendInvitation(*m_LockTxBuilder, isSender);
                SetState(State::Invitation, SubTxIndex::LOCK_TX);
            }
            return;
        }

        m_LockTxBuilder->GenerateSharedBlindingFactor();
        m_LockTxBuilder->SignPartial();

        if (lockTxState == State::Initial || lockTxState == State::Invitation)
        {
            m_LockTxBuilder->SharedUTXOProofPart2(isSender);
            SendBulletProofPart2(*m_LockTxBuilder, isSender);
            SetState(State::SharedUTXOProofPart2, SubTxIndex::LOCK_TX);
            return;
        }

        if (lockTxState == State::SharedUTXOProofPart2)
        {
            m_LockTxBuilder->SharedUTXOProofPart3(isSender);
            SendBulletProofPart3(*m_LockTxBuilder, isSender);
            SetState(State::SharedUTXOProofPart3, SubTxIndex::LOCK_TX);

            if (isSender)
            {
                // DEBUG
                assert(m_LockTxBuilder->GetPeerSignature());
                if (!m_LockTxBuilder->IsPeerSignatureValid())
                {
                    LOG_INFO() << GetTxID() << " Peer signature is invalid.";
                    return;
                }

                m_LockTxBuilder->FinalizeSignature();

                // TODO(alex.starun): create shared utxo ?
                m_LockTxBuilder->AddSharedOutput(amount);

                {
                    // TEST
                    auto tx = m_LockTxBuilder->CreateTransaction();
                    beam::TxBase::Context ctx;
                    tx->IsValid(ctx);
                }
            }
            return;
        }

        if (lockTxState == State::SharedUTXOProofPart3)
        {
            m_LockTxBuilder->ValidateSharedUTXO(isSender);
        }

    }

    void AtomicSwapTransaction::SendInvitation(const LockTxBuilder& lockBuilder, bool isSender)
    {
        Amount atomicSwapAmount = GetMandatoryParameter<Amount>(TxParameterID::AtomicSwapAmount);
        AtomicSwapCoin atomicSwapCoin = GetMandatoryParameter<AtomicSwapCoin>(TxParameterID::AtomicSwapCoin);

        SetTxParameter msg;
        msg.AddParameter(TxParameterID::Amount, lockBuilder.GetAmount())
            .AddParameter(TxParameterID::Fee, lockBuilder.GetFee())
            .AddParameter(TxParameterID::IsSender, !isSender)
            .AddParameter(TxParameterID::AtomicSwapAmount, atomicSwapAmount)
            .AddParameter(TxParameterID::AtomicSwapCoin, atomicSwapCoin)
            .AddParameter(TxParameterID::SubTxIndex, SubTxIndex::LOCK_TX)
            .AddParameter(TxParameterID::MinHeight, lockBuilder.GetMinHeight())
            .AddParameter(TxParameterID::PeerProtoVersion, s_ProtoVersion)
            .AddParameter(TxParameterID::PeerPublicExcess, lockBuilder.GetPublicExcess())
            .AddParameter(TxParameterID::PeerPublicNonce, lockBuilder.GetPublicNonce());

        if (!SendTxParameters(move(msg)))
        {
            OnFailed(TxFailureReason::FailedToSendParameters, false);
        }
    }

    void AtomicSwapTransaction::SendBulletProofPart2(const LockTxBuilder& lockBuilder, bool isSender)
    {
        SetTxParameter msg;
        msg.AddParameter(TxParameterID::SubTxIndex, SubTxIndex::LOCK_TX)
            .AddParameter(TxParameterID::PeerSignature, lockBuilder.GetPartialSignature())
            .AddParameter(TxParameterID::PeerOffset, lockBuilder.GetOffset());
        if (isSender)
        {
            auto proofPartialMultiSig = lockBuilder.GetProofPartialMultiSig();
            msg.AddParameter(TxParameterID::PeerSharedBulletProofMSig, proofPartialMultiSig);
        }
        else
        {
            auto bulletProof = lockBuilder.GetBulletProof();
            msg.AddParameter(TxParameterID::PeerProtoVersion, s_ProtoVersion)
                .AddParameter(TxParameterID::PeerPublicExcess, lockBuilder.GetPublicExcess())
                .AddParameter(TxParameterID::PeerPublicNonce, lockBuilder.GetPublicNonce())
                .AddParameter(TxParameterID::PeerPublicSharedBlindingFactor, lockBuilder.GetPublicSharedBlindingFactor())
                .AddParameter(TxParameterID::PeerSharedBulletProofPart2, bulletProof.m_Part2);
        }

        if (!SendTxParameters(move(msg)))
        {
            OnFailed(TxFailureReason::FailedToSendParameters, false);
        }
    }

    void AtomicSwapTransaction::SendBulletProofPart3(const LockTxBuilder& lockBuilder, bool isSender)
    {
        SetTxParameter msg;

        msg.AddParameter(TxParameterID::SubTxIndex, SubTxIndex::LOCK_TX);
        // if !isSender -> send p3
        // else send full bulletproof? output?

        if (!isSender)
        {
            auto bulletProof = lockBuilder.GetBulletProof();
            msg.AddParameter(TxParameterID::PeerSharedBulletProofPart3, bulletProof.m_Part3);
        }

        if (!SendTxParameters(move(msg)))
        {
            OnFailed(TxFailureReason::FailedToSendParameters, false);
        }
    }

    LockTxBuilder::LockTxBuilder(AtomicSwapTransaction& tx, SubTxID subTxID, Amount amount, Amount fee)
        : m_Tx{ tx }
        , m_SubTxID {subTxID}
        , m_Amount{ amount }
        , m_Fee{ fee }
        , m_Change{ 0 }
        , m_MinHeight{ 0 }
        , m_MaxHeight{ MaxHeight }
    {
    }

    const ECC::uintBig& LockTxBuilder::GetSharedSeed() const
    {
        return m_Seed;
    }

    const ECC::Scalar::Native& LockTxBuilder::GetSharedBlindingFactor() const
    {
        //
        return m_SharedBlindingFactor;
    }

    const ECC::RangeProof::Confidential& LockTxBuilder::GetBulletProof() const
    {
        return m_Bulletproof;
    }

    const ECC::RangeProof::Confidential::MultiSig& LockTxBuilder::GetProofPartialMultiSig() const
    {
        return m_ProofPartialMultiSig;
    }

    ECC::Point::Native LockTxBuilder::GetPublicSharedBlindingFactor() const
    {
        return Context::get().G * GetSharedBlindingFactor();
    }

    void LockTxBuilder::SharedUTXOProofPart2(bool shouldProduceMultisig)
    {
        if (shouldProduceMultisig)
        {
            Point::Native peerPublicSharedBlindingFactor;
            m_Tx.GetParameter(TxParameterID::PeerPublicSharedBlindingFactor, peerPublicSharedBlindingFactor, m_SubTxID);

            Point::Native commitment(Zero);
            // TODO: check pHGen
            Tag::AddValue(commitment, nullptr, GetAmount());
            commitment += GetPublicSharedBlindingFactor();
            commitment += peerPublicSharedBlindingFactor;

            m_CreatorParams.m_Kidv = m_SharedCoin.m_ID;
            beam::Output::GenerateSeedKid(m_CreatorParams.m_Seed.V, commitment, *m_Tx.GetWalletDB()->get_MasterKdf());

            Oracle oracle;
            oracle << (beam::Height)0; // CHECK, coin maturity

            m_Tx.GetParameter(TxParameterID::PeerSharedBulletProofPart2, m_Bulletproof.m_Part2, m_SubTxID);

            m_Bulletproof.CoSign(GetSharedSeed(), GetSharedBlindingFactor(), m_CreatorParams, oracle, RangeProof::Confidential::Phase::Step2, &m_ProofPartialMultiSig); // add last p2, produce msig
        }
        else
        {
            ZeroObject(m_Bulletproof.m_Part2);
            RangeProof::Confidential::MultiSig::CoSignPart(GetSharedSeed(), m_Bulletproof.m_Part2);
        }
    }

    void LockTxBuilder::SharedUTXOProofPart3(bool shouldProduceMultisig)
    {
        if (shouldProduceMultisig)
        {
            Oracle oracle;
            oracle << (beam::Height)0; // CHECK!

            m_Tx.GetParameter(TxParameterID::PeerSharedBulletProofPart3, m_Bulletproof.m_Part3, m_SubTxID);

            m_Bulletproof.CoSign(GetSharedSeed(), GetSharedBlindingFactor(), m_CreatorParams, oracle, RangeProof::Confidential::Phase::Finalize);

            {
                 // TEST
                Point::Native peerPublicSharedBlindingFactor;
                m_Tx.GetParameter(TxParameterID::PeerPublicSharedBlindingFactor, peerPublicSharedBlindingFactor, m_SubTxID);

                Point::Native commitment(Zero);
                // TODO: check pHGen
                Tag::AddValue(commitment, nullptr, GetAmount());
                commitment += GetPublicSharedBlindingFactor();
                commitment += peerPublicSharedBlindingFactor;

                Oracle oracleTest;
                oracleTest << (beam::Height)0;
                m_Bulletproof.IsValid(commitment, oracleTest, nullptr);
            }
        }
        else
        {
            m_Tx.GetParameter(TxParameterID::PeerSharedBulletProofMSig, m_ProofPartialMultiSig, m_SubTxID);

            ZeroObject(m_Bulletproof.m_Part3);
            m_ProofPartialMultiSig.CoSignPart(GetSharedSeed(), GetSharedBlindingFactor(), m_Bulletproof.m_Part3);
        }
    }

    void LockTxBuilder::ValidateSharedUTXO(bool shouldProduceMultisig)
    {

    }

    void LockTxBuilder::SelectInputs()
    {
        CoinIDList preselectedCoinIDs;
        vector<Coin> coins;
        Amount preselectedAmount = 0;
        if (m_Tx.GetParameter(TxParameterID::PreselectedCoins, preselectedCoinIDs, m_SubTxID) && !preselectedCoinIDs.empty())
        {
            coins = m_Tx.GetWalletDB()->getCoinsByID(preselectedCoinIDs);
            for (auto& coin : coins)
            {
                preselectedAmount += coin.getAmount();
                coin.m_spentTxId = m_Tx.GetTxID();
            }
            m_Tx.GetWalletDB()->save(coins);
        }
        Amount amountWithFee = GetAmount() + GetFee();
        if (preselectedAmount < amountWithFee)
        {
            auto selectedCoins = m_Tx.GetWalletDB()->selectCoins(amountWithFee - preselectedAmount);
            copy(selectedCoins.begin(), selectedCoins.end(), back_inserter(coins));
        }

        if (coins.empty())
        {
            Totals totals(*m_Tx.GetWalletDB());

            LOG_ERROR() << m_Tx.GetTxID() << " You only have " << PrintableAmount(totals.Avail);
            throw TransactionFailedException(!m_Tx.IsInitiator(), TxFailureReason::NoInputs);
        }

        m_Inputs.reserve(m_Inputs.size() + coins.size());
        Amount total = 0;
        for (auto& coin : coins)
        {
            coin.m_spentTxId = m_Tx.GetTxID();

            auto& input = m_Inputs.emplace_back(make_unique<Input>());

            Scalar::Native blindingFactor;
            m_Tx.GetWalletDB()->calcCommitment(blindingFactor, input->m_Commitment, coin.m_ID);

            m_Offset += blindingFactor;
            total += coin.m_ID.m_Value;
        }

        m_Change += total - amountWithFee;

        m_Tx.SetParameter(TxParameterID::Change, m_Change, false, m_SubTxID);
        m_Tx.SetParameter(TxParameterID::Inputs, m_Inputs, false, m_SubTxID);
        m_Tx.SetParameter(TxParameterID::Offset, m_Offset, false, m_SubTxID);

        m_Tx.GetWalletDB()->save(coins);
    }

    void LockTxBuilder::AddChangeOutput()
    {
        if (m_Change == 0)
        {
            return;
        }

        AddOutput(m_Change, true);
    }

    void LockTxBuilder::AddOutput(Amount amount, bool bChange)
    {
        m_Outputs.push_back(CreateOutput(amount, bChange));
    }

    bool LockTxBuilder::FinalizeOutputs()
    {
        m_Tx.SetParameter(TxParameterID::Outputs, m_Outputs, false, m_SubTxID);
        m_Tx.SetParameter(TxParameterID::Offset, m_Offset, false, m_SubTxID);

        // TODO: check transaction size here

        return true;
    }

    Output::Ptr LockTxBuilder::CreateOutput(Amount amount, bool bChange)
    {
        Coin newUtxo(amount);
        newUtxo.m_createTxId = m_Tx.GetTxID();
        if (bChange)
        {
            newUtxo.m_ID.m_Type = Key::Type::Change;
        }
        m_Tx.GetWalletDB()->store(newUtxo);

        Scalar::Native blindingFactor;
        Output::Ptr output = make_unique<Output>();
        output->Create(blindingFactor, *m_Tx.GetWalletDB()->get_ChildKdf(newUtxo.m_ID.m_SubIdx), newUtxo.m_ID, *m_Tx.GetWalletDB()->get_MasterKdf());

        blindingFactor = -blindingFactor;
        m_Offset += blindingFactor;

        return output;
    }

    void LockTxBuilder::AddSharedOutput(Amount amount)
    {
        Point::Native peerPublicSharedBlindingFactor;
        m_Tx.GetParameter(TxParameterID::PeerPublicSharedBlindingFactor, peerPublicSharedBlindingFactor, m_SubTxID);

        Point::Native commitment(Zero);
        Tag::AddValue(commitment, nullptr, GetAmount());
        commitment += GetPublicSharedBlindingFactor();
        commitment += peerPublicSharedBlindingFactor;

        Output::Ptr output = make_unique<Output>();
        output->m_Commitment = commitment;
        output->m_pConfidential = std::make_unique<ECC::RangeProof::Confidential>();
        *(output->m_pConfidential) = m_Bulletproof;

        m_Outputs.push_back(std::move(output));
    }

    void LockTxBuilder::CreateKernel()
    {
        // create kernel
        //assert(!m_Kernel);
        m_Kernel = make_unique<TxKernel>();
        m_Kernel->m_Fee = GetFee();
        m_Kernel->m_Height.m_Min = m_MinHeight;
        m_Kernel->m_Height.m_Max = m_MaxHeight;
        m_Kernel->m_Commitment = Zero;

        if (!m_Tx.GetParameter(TxParameterID::BlindingExcess, m_BlindingExcess, m_SubTxID))
        {
            Key::ID kid;
            kid.m_Idx = m_Tx.GetWalletDB()->AllocateKidRange(1);
            kid.m_Type = FOURCC_FROM(KerW);
            kid.m_SubIdx = 0;

            m_Tx.GetWalletDB()->get_MasterKdf()->DeriveKey(m_BlindingExcess, kid);

            m_Tx.SetParameter(TxParameterID::BlindingExcess, m_BlindingExcess, false, m_SubTxID);

            m_Offset += m_BlindingExcess;
        }

        m_BlindingExcess = -m_BlindingExcess;

        // Don't store the generated nonce for the kernel multisig. Instead - store the raw random, from which the nonce is derived using kdf.
        NoLeak<Hash::Value> hvRandom;
        if (!m_Tx.GetParameter(TxParameterID::MyNonce, hvRandom.V, m_SubTxID))
        {
            ECC::GenRandom(hvRandom.V);
            m_Tx.SetParameter(TxParameterID::MyNonce, hvRandom.V, false, m_SubTxID);
        }

        m_Tx.GetWalletDB()->get_MasterKdf()->DeriveKey(m_MultiSig.m_Nonce, hvRandom.V);
    }

    void LockTxBuilder::GenerateSharedBlindingFactor()
    {
        if (!m_Tx.GetParameter(TxParameterID::SharedBlindingFactor, m_SharedBlindingFactor, m_SubTxID))
        {
            // save BlindingFactor for shared UTXO
            // create output ???
            m_SharedCoin = m_Tx.GetWalletDB()->generateSharedCoin(GetAmount());

            // blindingFactor = sk + sk1

            beam::SwitchCommitment switchCommitment;
            switchCommitment.Create(m_SharedBlindingFactor, *m_Tx.GetWalletDB()->get_ChildKdf(m_SharedCoin.m_ID.m_SubIdx), m_SharedCoin.m_ID);

            m_Tx.SetParameter(TxParameterID::SharedBlindingFactor, m_SharedBlindingFactor, m_SubTxID);

            Oracle oracle;
            RangeProof::Confidential::GenerateSeed(m_Seed, m_SharedBlindingFactor, GetAmount(), oracle);

            ECC::Scalar::Native blindingFactor = -m_SharedBlindingFactor;
            m_Offset += blindingFactor;
            m_Tx.SetParameter(TxParameterID::Offset, m_Offset, false, m_SubTxID);
        }
    }

    Point::Native LockTxBuilder::GetPublicExcess() const
    {
        return Context::get().G * m_BlindingExcess;
    }

    Point::Native LockTxBuilder::GetPublicNonce() const
    {
        return Context::get().G * m_MultiSig.m_Nonce;
    }

    bool LockTxBuilder::GetPeerPublicExcessAndNonce()
    {
        return m_Tx.GetParameter(TxParameterID::PeerPublicExcess, m_PeerPublicExcess, m_SubTxID)
            && m_Tx.GetParameter(TxParameterID::PeerPublicNonce, m_PeerPublicNonce, m_SubTxID);
    }

    bool LockTxBuilder::GetPeerSignature()
    {
        if (m_Tx.GetParameter(TxParameterID::PeerSignature, m_PeerSignature, m_SubTxID))
        {
            LOG_DEBUG() << m_Tx.GetTxID() << " Received PeerSig:\t" << Scalar(m_PeerSignature);
            return true;
        }

        return false;
    }

    bool LockTxBuilder::GetInitialTxParams()
    {
        m_Tx.GetParameter(TxParameterID::Inputs, m_Inputs, m_SubTxID);
        m_Tx.GetParameter(TxParameterID::Outputs, m_Outputs, m_SubTxID);
        m_Tx.GetParameter(TxParameterID::MinHeight, m_MinHeight, m_SubTxID);
        m_Tx.GetParameter(TxParameterID::MaxHeight, m_MaxHeight, m_SubTxID);
        return m_Tx.GetParameter(TxParameterID::BlindingExcess, m_BlindingExcess, m_SubTxID)
            && m_Tx.GetParameter(TxParameterID::Offset, m_Offset, m_SubTxID);
    }

    bool LockTxBuilder::GetPeerInputsAndOutputs()
    {
        // used temporary vars to avoid non-short circuit evaluation
        bool hasInputs = m_Tx.GetParameter(TxParameterID::PeerInputs, m_PeerInputs, m_SubTxID);
        bool hasOutputs = (m_Tx.GetParameter(TxParameterID::PeerOutputs, m_PeerOutputs, m_SubTxID)
            && m_Tx.GetParameter(TxParameterID::PeerOffset, m_PeerOffset, m_SubTxID));
        return hasInputs || hasOutputs;
    }

    void LockTxBuilder::SignPartial()
    {
        // create signature
        Point::Native totalPublicExcess = GetPublicExcess();
        totalPublicExcess += m_PeerPublicExcess;
        m_Kernel->m_Commitment = totalPublicExcess;

        m_Kernel->get_Hash(m_Message);
        m_MultiSig.m_NoncePub = GetPublicNonce() + m_PeerPublicNonce;
        m_MultiSig.SignPartial(m_PartialSignature, m_Message, m_BlindingExcess);
        StoreKernelID();
    }

    void LockTxBuilder::FinalizeSignature()
    {
        // final signature
        m_Kernel->m_Signature.m_NoncePub = GetPublicNonce() + m_PeerPublicNonce;
        m_Kernel->m_Signature.m_k = m_PartialSignature + m_PeerSignature;

        StoreKernelID();
    }

    Transaction::Ptr LockTxBuilder::CreateTransaction()
    {
        assert(m_Kernel);
        LOG_INFO() << m_Tx.GetTxID() << " Transaction created. Kernel: " << GetKernelIDString();

        m_Tx.GetParameter(TxParameterID::PeerOffset, m_PeerOffset, m_SubTxID);

        // create transaction
        auto transaction = make_shared<Transaction>();
        transaction->m_vKernels.push_back(move(m_Kernel));
        transaction->m_Offset = m_Offset + m_PeerOffset;
        transaction->m_vInputs = move(m_Inputs);
        transaction->m_vOutputs = move(m_Outputs);
        move(m_PeerInputs.begin(), m_PeerInputs.end(), back_inserter(transaction->m_vInputs));
        move(m_PeerOutputs.begin(), m_PeerOutputs.end(), back_inserter(transaction->m_vOutputs));

        transaction->Normalize();

        return transaction;
    }

    bool LockTxBuilder::IsPeerSignatureValid() const
    {
        Signature peerSig;
        peerSig.m_NoncePub = m_MultiSig.m_NoncePub;
        peerSig.m_k = m_PeerSignature;
        return peerSig.IsValidPartial(m_Message, m_PeerPublicNonce, m_PeerPublicExcess);
    }

    Amount LockTxBuilder::GetAmount() const
    {
        return m_Amount;
    }

    Amount LockTxBuilder::GetFee() const
    {
        return m_Fee;
    }

    Height LockTxBuilder::GetMinHeight() const
    {
        return m_MinHeight;
    }

    Height LockTxBuilder::GetMaxHeight() const
    {
        return m_MaxHeight;
    }

    const vector<Input::Ptr>& LockTxBuilder::GetInputs() const
    {
        return m_Inputs;
    }

    const vector<Output::Ptr>& LockTxBuilder::GetOutputs() const
    {
        return m_Outputs;
    }

    const Scalar::Native& LockTxBuilder::GetOffset() const
    {
        return m_Offset;
    }

    const Scalar::Native& LockTxBuilder::GetPartialSignature() const
    {
        return m_PartialSignature;
    }

    const TxKernel& LockTxBuilder::GetKernel() const
    {
        assert(m_Kernel);
        return *m_Kernel;
    }

    void LockTxBuilder::StoreKernelID()
    {
        assert(m_Kernel);
        Merkle::Hash kernelID;
        m_Kernel->get_ID(kernelID);

        m_Tx.SetParameter(TxParameterID::KernelID, kernelID, m_SubTxID);
    }

    string LockTxBuilder::GetKernelIDString() const
    {
        Merkle::Hash kernelID;
        m_Kernel->get_ID(kernelID);

        char sz[Merkle::Hash::nTxtLen + 1];
        kernelID.Print(sz);
        return string(sz);
    }
} // namespace