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

#include "local_private_key_keeper.h"
#include "utility/logger.h"

namespace beam::wallet
{
    using namespace ECC;
    using namespace std;
    
    namespace
    {
        const char* LOCAL_NONCE_SEEDS = "NonceSeeds";
        const size_t kMaxNonces = 1000000;
    }

    LocalPrivateKeyKeeper::LocalPrivateKeyKeeper(IVariablesDB::Ptr walletDB, Key::IKdf::Ptr kdf)
        : m_Variables(walletDB)
        , m_MasterKdf(kdf)
    {
        LoadNonceSeeds();
    }

    LocalPrivateKeyKeeper::~LocalPrivateKeyKeeper()
    {

    }


    void LocalPrivateKeyKeeper::GeneratePublicKeys(const vector<CoinID>& ids, bool createCoinKey, Callback<PublicKeys>&& resultCallback, ExceptionCallback&& exceptionCallback)
    {
        DoAsync([=]() { return GeneratePublicKeysSync(ids, createCoinKey); }, move(resultCallback), move(exceptionCallback));
    }

    void LocalPrivateKeyKeeper::GenerateOutputs(Height schemeHeight, const std::vector<CoinID>& ids, Callback<Outputs>&& resultCallback, ExceptionCallback&& exceptionCallback)
    {
        DoThreadAsync([=]() { return GenerateOutputsSync(schemeHeight, ids); }, std::move(resultCallback), std::move(exceptionCallback));
    }

    void LocalPrivateKeyKeeper::SignReceiver(const std::vector<CoinID>& inputs
                                           , const std::vector<CoinID>& outputs
                                           , const KernelParameters& kernelParameters
                                           , const WalletIDKey& walletIDkey
                                           , Callback<ReceiverSignature>&& resultCallback
                                           , ExceptionCallback&& exceptionCallback)
    {
        DoAsync([=]() { return SignReceiverSync(inputs, outputs, kernelParameters, walletIDkey); }, move(resultCallback), move(exceptionCallback));
    }

    void LocalPrivateKeyKeeper::SignSender(const std::vector<CoinID>& inputs
                                         , const std::vector<CoinID>& outputs
                                         , size_t nonceSlot
                                         , const KernelParameters& kernelParameters
                                         , bool initial
                                         , Callback<SenderSignature>&& resultCallback
                                         , ExceptionCallback&& exceptionCallback)
    {
        DoAsync([=]() { return SignSenderSync(inputs, outputs, nonceSlot, kernelParameters, initial); }, move(resultCallback), move(exceptionCallback));
    }


    ////////


    size_t LocalPrivateKeyKeeper::AllocateNonceSlotSync()
    {
        ++m_NonceSlotLast %= kMaxNonces;

        if (m_NonceSlotLast >= m_Nonces.size())
        {
            m_NonceSlotLast = m_Nonces.size();
            m_Nonces.emplace_back();
        }

        // Don't store the generated nonce for the kernel multisig. Instead - store the raw random, from which the nonce is derived using kdf.

        ECC::GenRandom(m_Nonces[m_NonceSlotLast].V);

        SaveNonceSeeds();

        return m_NonceSlotLast;
    }

    ////

    IPrivateKeyKeeper::PublicKeys LocalPrivateKeyKeeper::GeneratePublicKeysSync(const std::vector<CoinID>& ids, bool createCoinKey)
    {
        PublicKeys result;
        Scalar::Native secretKey;
        result.reserve(ids.size());
        if (createCoinKey)
        {
            for (const auto& coinID : ids)
            {
                Point& publicKey = result.emplace_back();
                CoinID::Worker(coinID).Create(secretKey, publicKey, *GetChildKdf(coinID));
            }
        }
        else
        {
            for (const auto& keyID : ids)
            {
                Point& publicKey = result.emplace_back();
                m_MasterKdf->DeriveKey(secretKey, keyID);
                publicKey = Context::get().G * secretKey;
            }
        }
        return result;
    }

    ECC::Point LocalPrivateKeyKeeper::GeneratePublicKeySync(const ECC::uintBig& val)
    {
        Scalar::Native secretKey;
        Point publicKey;

        m_MasterKdf->DeriveKey(secretKey, val);
        publicKey = Context::get().G * secretKey;

        return publicKey;
    }

    ECC::Point LocalPrivateKeyKeeper::GenerateCoinKeySync(const CoinID& id)
    {
        Scalar::Native secretKey;
        Point publicKey;
        CoinID::Worker(id).Create(secretKey, publicKey, *GetChildKdf(id));
        return publicKey;
    }

    IPrivateKeyKeeper::Outputs LocalPrivateKeyKeeper::GenerateOutputsSync(Height schemeHeigh, const std::vector<CoinID>& ids)
    {
        Outputs result;
        Scalar::Native secretKey;
        result.reserve(ids.size());
        for (const auto& coinID : ids)
        {
            auto& output = result.emplace_back(make_unique<Output>());
            output->Create(schemeHeigh, secretKey, *GetChildKdf(coinID), coinID, *m_MasterKdf);
        }
        return result;
    }

    ECC::Point LocalPrivateKeyKeeper::GenerateNonceSync(size_t slot)
    {
        Point::Native result = Context::get().G * GetNonce(slot);
        return result;
    }

    ReceiverSignature LocalPrivateKeyKeeper::SignReceiverSync(const std::vector<CoinID>& inputs
                                                        , const std::vector<CoinID>& outputs
                                                        , const KernelParameters& kernelParameters
                                                        , const WalletIDKey& walletIDkey)
    {
        ReceiverSignature res;
        auto value = CalculateValue(inputs, outputs);
        if (value > 0 && !inputs.empty() && 
            (Amount(value) == kernelParameters.fee // self tx
                || outputs.empty())) // spending shared utxo
        {
            value = 0;
        }
        else if (value >= 0)
        {
            throw KeyKeeperException("Receover failed to sign tx. We are not receiving");
        }
       
        auto excess = GetExcess(inputs, outputs, Zero);
        Amount val = -value;

        Scalar::Native kKrn, kNonce;
        
        ECC::Hash::Value hv;
        Hash::Processor()
            << kernelParameters.fee
            << kernelParameters.height.m_Min
            << kernelParameters.height.m_Max
            << kernelParameters.commitment
            << kernelParameters.publicNonce
            << kernelParameters.peerID
            << excess
            << val >> hv;

        NonceGenerator ng("hw-wlt-rcv");
        ng << hv;

        ng >> kKrn;
        ng >> kNonce; 

        Point::Native commitment;
        if (!commitment.Import(kernelParameters.commitment))
        {
            throw InvalidParametersException();
        }
        
        Point::Native temp;
        temp = Context::get().G * kKrn; // public kernel commitment
        commitment += temp;
        

        TxKernelStd kernel;
        kernel.m_Commitment = commitment;
        kernel.m_Fee = kernelParameters.fee;
        kernel.m_Height = kernelParameters.height;
        if (kernelParameters.lockImage || kernelParameters.lockPreImage)
        {
            kernel.m_pHashLock = make_unique<TxKernelStd::HashLock>();
        
            if (kernelParameters.lockPreImage)
                kernel.m_pHashLock->m_Value = *kernelParameters.lockPreImage;
            else
            {
                kernel.m_pHashLock->m_Value = *kernelParameters.lockImage;
                kernel.m_pHashLock->m_IsImage = true;
            }
        }
        kernel.UpdateID();
        const Merkle::Hash& message = kernel.m_Internal.m_ID;
        temp = Context::get().G * kNonce; // public receiver nonce, we don't need slots here since we sign transaction only once
        Point::Native pt;
        if (!pt.Import(kernelParameters.publicNonce))
        {
            throw InvalidParametersException();
        }

        res.m_KernelCommitment = kernel.m_Commitment;
        res.m_KernelSignature.m_NoncePub = pt + temp;
        res.m_KernelSignature.SignPartial(message, kKrn, kNonce);
        kKrn = -kKrn;
        excess += kKrn;
        res.m_Offset = excess;

        if (walletIDkey)
        {
            PaymentConfirmation pc;
            pc.m_KernelID = kernel.m_Internal.m_ID;
            pc.m_Sender = kernelParameters.peerID;
            pc.m_Value = val;

            auto keyPair = GetWalletID(walletIDkey);

            pc.Sign(keyPair.m_PrivateKey);

            res.m_PaymentProofSignature = pc.m_Signature;
        }
        
        return res;
    }

    SenderSignature LocalPrivateKeyKeeper::SignSenderSync(const std::vector<CoinID>& inputs
                                                    , const std::vector<CoinID>& outputs
                                                    , size_t nonceSlot
                                                    , const KernelParameters& kernelParameters
                                                    , bool initial)
    {
        SenderSignature res;
        auto value = CalculateValue(inputs, outputs);

        value -= kernelParameters.fee;

        if (value < 0)
        {
            throw KeyKeeperException("Sender failed to sign tx. We are not sending");
        }

        auto excess = GetExcess(inputs, outputs, Zero);

        Scalar::Native kKrn;
        ECC::Hash::Value hv;
        Hash::Processor()
            << kernelParameters.fee
            << kernelParameters.height.m_Min
            // << kernelParameters.height.m_Max
            //<< kernelParameters.peerID 
            << excess
            << Amount(value) >> hv;

        NonceGenerator ng("hw-wlt-snd");
        ng << hv;

        ng >> kKrn;

        Point::Native commitment;
        commitment = Context::get().G * kKrn; // public kernel commitment

        Scalar::Native nonce = GetNonce(nonceSlot);
        Point::Native myPublicNonce;
        myPublicNonce = Context::get().G * nonce;
        if (initial)
        {
            res.m_KernelCommitment = commitment;
            res.m_KernelSignature.m_NoncePub = myPublicNonce;
            return res;
        }

        TxKernelStd kernel;
        kernel.m_Commitment = kernelParameters.commitment;
        kernel.m_Fee = kernelParameters.fee;
        kernel.m_Height = kernelParameters.height;
        if (kernelParameters.lockImage || kernelParameters.lockPreImage)
        {
            kernel.m_pHashLock = make_unique<TxKernelStd::HashLock>();

            if (kernelParameters.lockPreImage)
                kernel.m_pHashLock->m_Value = *kernelParameters.lockPreImage;
            else
            {
                kernel.m_pHashLock->m_Value = *kernelParameters.lockImage;
                kernel.m_pHashLock->m_IsImage = true;
            }
        }
        kernel.UpdateID();
        const Merkle::Hash& message = kernel.m_Internal.m_ID;

        // TODO: temporal solution
        if (kernelParameters.myID != Zero && kernelParameters.peerID != Zero)
        {
            ////////////////////////
            // Verify peer signature
            PaymentConfirmation pc;
            pc.m_KernelID = message;
            pc.m_Value = Amount(value);
            pc.m_Sender = kernelParameters.myID;
            pc.m_Signature = kernelParameters.paymentProofSignature;

            if (!pc.IsValid(kernelParameters.peerID))
            {
                throw InvalidPaymentProofException();
            }
        }
        
        /////////////////////////
        // Ask for user permission!
        //
        // ...

        if (!commitment.Import(kernelParameters.commitment))
        {
            throw InvalidParametersException();
        }

        ECC::GenRandom(m_Nonces[nonceSlot].V); // Invalidate slot immediately after using it (to make it similar to HW wallet)!

        res.m_KernelSignature.m_NoncePub = kernelParameters.publicNonce;
        res.m_KernelSignature.SignPartial(message, kKrn, nonce);

        kKrn = -kKrn;
        excess += kKrn;
        res.m_Offset = excess;

        return res;
    }

    Key::IKdf::Ptr LocalPrivateKeyKeeper::get_SbbsKdf() const
    {
        return m_MasterKdf;
    }

    void LocalPrivateKeyKeeper::LoadNonceSeeds()
    {
        try
        {
            ByteBuffer buffer;
            if (m_Variables->getBlob(LOCAL_NONCE_SEEDS, buffer) && !buffer.empty())
            {
                Deserializer d;
                d.reset(buffer);
                d& m_Nonces;
                d& m_NonceSlotLast;
            }
        }
        catch (...)
        {
            m_Nonces.clear();
        }

        if (m_NonceSlotLast >= m_Nonces.size())
            m_NonceSlotLast = m_Nonces.size() - 1;
    }

    void LocalPrivateKeyKeeper::SaveNonceSeeds()
    {
        Serializer s;
        s& m_Nonces;
        s& m_NonceSlotLast;
        ByteBuffer buffer;
        s.swap_buf(buffer);
        m_Variables->setVarRaw(LOCAL_NONCE_SEEDS, buffer.data(), buffer.size());
    }


    LocalPrivateKeyKeeper::KeyPair LocalPrivateKeyKeeper::GetWalletID(const WalletIDKey& walletKeyID) const
    {
        Key::ID kid(walletKeyID, Key::Type::WalletID);
        LocalPrivateKeyKeeper::KeyPair res;
        m_MasterKdf->DeriveKey(res.m_PrivateKey, kid);
        beam::proto::Sk2Pk(res.m_PublicKey, res.m_PrivateKey);
        return res;
    }

    ////

    Key::IKdf::Ptr LocalPrivateKeyKeeper::GetChildKdf(const CoinID& cid) const
    {
        return cid.get_ChildKdf(m_MasterKdf);
    }

    Scalar::Native LocalPrivateKeyKeeper::GetNonce(size_t slot)
    {
        const auto& randomValue = m_Nonces[slot].V;

        Scalar::Native nonce;
        m_MasterKdf->DeriveKey(nonce, randomValue);

        return nonce;
    }

    Scalar::Native LocalPrivateKeyKeeper::GetExcess(const std::vector<CoinID>& inputs, const std::vector<CoinID>& outputs, const ECC::Scalar::Native& offset) const
    {
        // Excess = Sum(input blinfing factors) - Sum(output blinfing factors) - offset
        Point commitment;
        Scalar::Native blindingFactor;
        Scalar::Native excess = offset;

        for (const auto& coinID : outputs)
        {
            CoinID::Worker(coinID).Create(blindingFactor, commitment, *GetChildKdf(coinID));
            excess += blindingFactor;
        }
        excess = -excess;
        for (const auto& coinID : inputs)
        {
            CoinID::Worker(coinID).Create(blindingFactor, commitment, *GetChildKdf(coinID));
            excess += blindingFactor;
        }

        return excess;
    }

    int64_t LocalPrivateKeyKeeper::CalculateValue(const std::vector<CoinID>& inputs, const std::vector<CoinID>& outputs) const
    {
        // TODO: sum different assets separately!

        int64_t value = 0;
        for (const auto& coinID : outputs)
        {
            value += coinID.m_Value;
        }

        value = -value;

        for (const auto& coinID : inputs)
        {
            value += coinID.m_Value;
        }

        return value;
    }

    void LocalPrivateKeyKeeper::SignAssetKernel(const std::vector<CoinID>& inputs,
                const std::vector<CoinID>& outputs,
                Amount fee,
                Key::Index assetOwnerIdx,
                TxKernelAssetControl& kernel,
                Callback<AssetSignature>&& resultCallback,
                ExceptionCallback&& exceptionCallback)
    {
         DoAsync([&]()
            {
                return SignAssetKernelSync(inputs, outputs, fee, assetOwnerIdx, kernel);
            },
            move(resultCallback),
            move(exceptionCallback)
         );
    }

    AssetSignature LocalPrivateKeyKeeper::SignAssetKernelSync(const std::vector<CoinID>& inputs,
            const std::vector<CoinID>& outputs,
            Amount fee,
            Key::Index assetOwnerIdx,
            TxKernelAssetControl& kernel)
    {
        auto value = CalculateValue(inputs, outputs);
        value -= fee;

        if (value < 0)
        {
            throw KeyKeeperException("Failed to sign asset kernel. Input amount is not enough");
        }

        const auto& keypair = GetAssetOwnerKeypair(assetOwnerIdx);
        kernel.m_Owner = keypair.first;

        ECC::Scalar::Native kernelSk;
        m_MasterKdf->DeriveKey(kernelSk, Key::ID(assetOwnerIdx, Key::Type::Kernel, assetOwnerIdx));
        kernel.Sign(kernelSk, keypair.second);

        kernelSk = -kernelSk;
        auto excess = GetExcess(inputs, outputs, Zero);
        excess += kernelSk;

        AssetSignature result;
        result.m_Offset = excess;
        result.m_AssetOwnerId = keypair.first;
        return result;
    }

    std::pair<PeerID, ECC::Scalar::Native> LocalPrivateKeyKeeper::GetAssetOwnerKeypair(Key::Index assetOwnerIdx)
    {
        Scalar::Native skAssetOwnerSk;
        m_MasterKdf->DeriveKey(skAssetOwnerSk, beam::Key::ID(assetOwnerIdx, beam::Key::Type::Asset));

        beam::PeerID assetOwnerId;
        beam::proto::Sk2Pk(assetOwnerId, skAssetOwnerSk);

        return std::make_pair(assetOwnerId, std::move(skAssetOwnerSk));
    }

    PeerID LocalPrivateKeyKeeper::GetAssetOwnerID(Key::Index assetOwnerIdx)
    {
        return GetAssetOwnerKeypair(assetOwnerIdx).first;
    }



    /////////////////////////
    // LocalPrivateKeyKeeper2
    LocalPrivateKeyKeeper2::LocalPrivateKeyKeeper2(const Key::IKdf::Ptr& pKdf)
        :m_pKdf(pKdf)
    {
    }

    IPrivateKeyKeeper2::Status::Type LocalPrivateKeyKeeper2::ToImage(Point::Native& res, uint32_t iGen, const Scalar::Native& sk)
    {
        const Generator::Obscured* pGen;

        switch (iGen)
        {
        case 0: pGen = &Context::get().G; break;
        case 1: pGen = &Context::get().J; break;
        case 2: pGen = &Context::get().H_Big; break;
        default:
            return Status::Unspecified;
        }

        res = (*pGen) * sk;
        return Status::Success;
    }

    bool LocalPrivateKeyKeeper2::Aggregate(Aggregation& aggr, const Method::TxCommon& tx, bool bSending)
    {
        if (tx.m_KernelParams.m_Height.m_Min < Rules::get().pForks[1].m_Height)
            return false; // disallow weak scheme

        aggr.m_AssetID = 0;
        aggr.m_ValBig = Zero;
        aggr.m_ValBigAsset = Zero;

        if (!Aggregate(aggr, tx.m_vInputs, false))
            return false;

        aggr.m_sk = -aggr.m_sk;
        aggr.m_ValBig.Negate();
        aggr.m_ValBigAsset.Negate();

        if (!Aggregate(aggr, tx.m_vOutputs, true))
            return false;

        if (bSending)
        {
            aggr.m_ValBig.Negate();
            aggr.m_ValBigAsset.Negate();
        }

        if (AmountBig::get_Hi(aggr.m_ValBig) || AmountBig::get_Hi(aggr.m_ValBigAsset))
            return false; // overflow or incorrect balance (snd/rcv)

        aggr.m_Val = AmountBig::get_Lo(aggr.m_ValBig);
        aggr.m_ValAsset = AmountBig::get_Lo(aggr.m_ValBigAsset);
        return true;
    }

    bool LocalPrivateKeyKeeper2::Aggregate(Aggregation& aggr, const std::vector<CoinID>& v, bool bOuts)
    {
        Scalar::Native sk;
        for (size_t i = 0; i < v.size(); i++)
        {
            const CoinID& cid = v[i];

            if (cid.get_Scheme() < Key::IDV::Scheme::V1)
            {
                // disallow weak scheme
                if (bOuts)
                    return false; // no reason to create new weak outputs

                if (IsTrustless())
                    return false;
            }

            uintBigFor<Amount>::Type val = cid.m_Value;

            if (cid.m_AssetID)
            {
                if (aggr.m_AssetID)
                {
                    if (aggr.m_AssetID != cid.m_AssetID)
                        return false;
                }
                else
                    aggr.m_AssetID = cid.m_AssetID;

                aggr.m_ValBigAsset += val;
            }
            else
                aggr.m_ValBig += val;

            CoinID::Worker(cid).Create(sk, *cid.get_ChildKdf(m_pKdf));
            aggr.m_sk += sk;
        }

        return true;
    }

    IPrivateKeyKeeper2::Status::Type LocalPrivateKeyKeeper2::InvokeSync(Method::get_Kdf& x)
    {
        Key::IKdf::Ptr pKdf = x.m_Root ?
            m_pKdf :
            MasterKey::get_Child(*m_pKdf, x.m_iChild);

        x.m_pPKdf = pKdf;

        if (!IsTrustless())
            x.m_pKdf = pKdf;

        return Status::Success;
    }

    IPrivateKeyKeeper2::Status::Type LocalPrivateKeyKeeper2::InvokeSync(Method::get_NumSlots& x)
    {
        x.m_Count = get_NumSlots();
        return Status::Success;
    }

    IPrivateKeyKeeper2::Status::Type LocalPrivateKeyKeeper2::InvokeSync(Method::CreateOutput& x)
    {
        if (x.m_Result.m_Coinbase || x.m_Result.m_pPublic || x.m_Result.m_RecoveryOnly || (x.m_hScheme < Rules::get().pForks[1].m_Height))
            return Status::Unspecified; // not allowed

        if (x.m_Result.m_Incubation || (x.m_Cid.get_Scheme() < Key::IDV::Scheme::V1))
        {
            if (IsTrustless())
                return Status::UserAbort; // or maybe ask user
        }

        Scalar::Native sk;
        x.m_Result.Create(x.m_hScheme, sk, *x.m_Cid.get_ChildKdf(m_pKdf), x.m_Cid, *m_pKdf);

        return Status::Success;
    }

    void LocalPrivateKeyKeeper2::UpdateOffset(Method::TxCommon& tx, const Scalar::Native& kDiff, const Scalar::Native& kKrn)
    {
        Scalar::Native k = kDiff + kKrn;
        k = -k;
        tx.m_kOffset += k;
    }

    IPrivateKeyKeeper2::Status::Type LocalPrivateKeyKeeper2::InvokeSync(Method::SignReceiver& x)
    {
        Aggregation aggr;
        if (!Aggregate(aggr, x, false))
            return Status::Unspecified;

        if (aggr.m_Val)
        {
            if (aggr.m_AssetID)
                return Status::Unspecified; // can't receive both

            aggr.m_ValAsset = aggr.m_Val;
        }
        else
        {
            if (!aggr.m_ValAsset)
                return Status::Unspecified; // should receive at least something
        }

        Scalar::Native kKrn, kNonce;

        // Hash *ALL* the parameters (maybe just hash-serialize the whole request)
        Hash::Value hv;
        Hash::Processor()
            << x.m_KernelParams.m_Fee
            << x.m_KernelParams.m_Height.m_Min
            << x.m_KernelParams.m_Height.m_Max
            << x.m_KernelParams.m_Commitment
            << x.m_KernelParams.m_Signature.m_NoncePub
            << x.m_Peer
            << x.m_MyID
            << aggr.m_sk
            << aggr.m_ValAsset
            << aggr.m_AssetID
            >> hv;

        NonceGenerator ng("hw-wlt-rcv");
        ng << hv;
        ng >> kKrn;
        ng >> kNonce;

        Point::Native commitment;
        if (!commitment.Import(x.m_KernelParams.m_Commitment))
            return Status::Unspecified;

        Point::Native temp;
        temp = Context::get().G * kKrn; // public kernel commitment
        commitment += temp;
        x.m_KernelParams.m_Commitment = commitment;


        if (!commitment.Import(x.m_KernelParams.m_Signature.m_NoncePub))
            return Status::Unspecified;

        temp = Context::get().G * kNonce;
        commitment += temp;
        x.m_KernelParams.m_Signature.m_NoncePub = commitment;

        TxKernelStd krn;
        x.m_KernelParams.To(krn);
        krn.UpdateID();
        const Merkle::Hash& msg = krn.m_Internal.m_ID;

        x.m_KernelParams.m_Signature.SignPartial(msg, kKrn, kNonce);
        UpdateOffset(x, aggr.m_sk, kKrn);

        if (x.m_MyID)
        {
            PaymentConfirmation pc;
            pc.m_KernelID = krn.m_Internal.m_ID;
            pc.m_Sender = x.m_Peer;
            pc.m_Value = aggr.m_ValAsset;
            pc.m_AssetID = aggr.m_AssetID;

            m_pKdf->DeriveKey(kKrn, Key::ID(x.m_MyID, Key::Type::WalletID));

            PeerID wid;
            beam::proto::Sk2Pk(wid, kKrn);
            pc.Sign(kKrn);

            x.m_PaymentProofSignature = pc.m_Signature;
        }

        return Status::Success;
    }

    IPrivateKeyKeeper2::Status::Type LocalPrivateKeyKeeper2::InvokeSync(Method::SignSender& x)
    {
        Aggregation aggr;
        if (!Aggregate(aggr, x, true))
            return Status::Unspecified;

        if (aggr.m_AssetID)
        {
            if (!aggr.m_ValAsset)
                return Status::Unspecified;

            if (aggr.m_Val != x.m_KernelParams.m_Fee)
                return Status::Unspecified; // all beams must be consumed by the fee
        }
        else
        {
            if (aggr.m_Val <= x.m_KernelParams.m_Fee)
                return Status::Unspecified;

            aggr.m_ValAsset = aggr.m_Val - x.m_KernelParams.m_Fee;
        }

        if (x.m_nonceSlot >= get_NumSlots())
            return Status::Unspecified;

        Scalar::Native kNonce;
        get_Nonce(kNonce, x.m_nonceSlot);

        // during negotiation kernel height can be adjusted. Commit however to other values
        Hash::Value hv;
        Hash::Processor()
            << x.m_KernelParams.m_Fee
            << x.m_Peer
            << x.m_MyID
            << aggr.m_sk
            << (Amount) aggr.m_ValAsset
            << aggr.m_AssetID
            << kNonce
            >> hv;

        Scalar::Native kKrn;

        NonceGenerator ng("hw-wlt-snd");
        ng << hv;
        ng >> kKrn;

        Hash::Processor()
            << "tx.token"
            << kKrn
            >> hv;

        if (hv == Zero)
            hv = 1U;

        if (x.m_UserAgreement == Zero)
        {
            if (IsTrustless())
            {
                if (x.m_Peer == Zero)
                    return Status::UserAbort; // user should not approve anonymous payment

                Status::Type res = ConfirmSpend(aggr.m_ValAsset, aggr.m_AssetID, x.m_Peer);
                if (Status::Success != res)
                    return res;
            }

            x.m_UserAgreement = hv;

            Point::Native commitment = Context::get().G * kKrn;
            x.m_KernelParams.m_Commitment = commitment;

            commitment = Context::get().G * kNonce;
            x.m_KernelParams.m_Signature.m_NoncePub = commitment;

            return Status::Success;
        }

        if (x.m_UserAgreement != hv)
            return Status::Unspecified; // incorrect user agreement token

        TxKernelStd krn;
        x.m_KernelParams.To(krn);
        krn.UpdateID();
        const Merkle::Hash& msg = krn.m_Internal.m_ID;

        if (x.m_Peer != Zero)
        {
            PaymentConfirmation pc;
            pc.m_KernelID = krn.m_Internal.m_ID;
            pc.m_Value = aggr.m_ValAsset;
            pc.m_AssetID = aggr.m_AssetID;

            if (x.m_MyID)
            {
                Scalar::Native skId;
                m_pKdf->DeriveKey(skId, Key::ID(x.m_MyID, Key::Type::WalletID));

                beam::proto::Sk2Pk(pc.m_Sender, skId);
            }
            else
                pc.m_Sender = Zero;

            pc.m_Signature = x.m_PaymentProofSignature;
            if (!pc.IsValid(x.m_Peer))
                return Status::Unspecified;
        }

        Regenerate(x.m_nonceSlot);

        Scalar::Native kSig = x.m_KernelParams.m_Signature.m_k;
        x.m_KernelParams.m_Signature.SignPartial(msg, kKrn, kNonce);
        kSig += x.m_KernelParams.m_Signature.m_k;
        x.m_KernelParams.m_Signature.m_k = kSig;

        UpdateOffset(x, aggr.m_sk, kKrn);

        return Status::Success;
    }

    /////////////////////////
    // LocalPrivateKeyKeeperStd
    void LocalPrivateKeyKeeperStd::State::Generate()
    {
        for (uint32_t i = 0; i < s_Slots; i++)
            Regenerate(i);
    }

    uint32_t LocalPrivateKeyKeeperStd::get_NumSlots()
    {
        return s_Slots;
    }

    void LocalPrivateKeyKeeperStd::get_Nonce(ECC::Scalar::Native& ret, uint32_t iSlot)
    {
        assert(iSlot < s_Slots);
        m_pKdf->DeriveKey(ret, m_State.m_pSlot[iSlot]);
    }

    void LocalPrivateKeyKeeperStd::State::Regenerate(uint32_t iSlot)
    {
        assert(iSlot < s_Slots);
        Hash::Processor() << m_hvLast >> m_hvLast;
        m_pSlot[iSlot] = m_hvLast;
    }

    void LocalPrivateKeyKeeperStd::Regenerate(uint32_t iSlot)
    {
        m_State.Regenerate(iSlot);
    }

} // namespace beam::wallet
