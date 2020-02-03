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
        res.m_PublicKey.FromSk(res.m_PrivateKey);
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
        kernel.Sign_(kernelSk, keypair.second);

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
        assetOwnerId.FromSk(skAssetOwnerSk);

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

    struct LocalPrivateKeyKeeper2::Aggregation
    {
        LocalPrivateKeyKeeper2& m_This;
        Aggregation(LocalPrivateKeyKeeper2& v) :m_This(v) {}

        ECC::Scalar::Native m_sk;
        Asset::ID m_AssetID;
        bool m_NonConventional;

        static bool Add(Amount& trg, Amount val)
        {
            trg += val;
            return (trg >= val); // test for overflow
        }

        static bool Sub(Amount& trg, Amount val)
        {
            if (trg < val)
                return false;
            trg -= val;
            return true;
        }

        struct Values
        {
            Amount m_Beam;
            Amount m_Asset;

            bool Subtract(const Values& v)
            {
                return
                    Sub(m_Beam, v.m_Beam) &&
                    Sub(m_Asset, v.m_Asset);
            }
        };

        Values m_Ins;
        Values m_Outs;

        bool Aggregate(const Method::TxCommon&);
        bool Aggregate(const std::vector<CoinID>&, bool bOuts);
    };

    bool LocalPrivateKeyKeeper2::Aggregation::Aggregate(const Method::TxCommon& tx)
    {
        if (!tx.m_pKernel)
            return false;
        TxKernelStd& krn = *tx.m_pKernel;

        m_NonConventional = tx.m_NonConventional;
        if (m_NonConventional)
        {
            if (m_This.IsTrustless())
                return false;
        }
        else
        {
            if (krn.m_CanEmbed ||
                krn.m_pHashLock ||
                krn.m_pRelativeLock ||
                !krn.m_vNested.empty())
                return false; // non-trivial kernels should not be supported

            if ((krn.m_Height.m_Min < Rules::get().pForks[1].m_Height) && m_This.IsTrustless())
                return false; // disallow weak scheme
        }

        m_AssetID = 0;

        if (!Aggregate(tx.m_vInputs, false))
            return false;

        m_sk = -m_sk;

        if (!Aggregate(tx.m_vOutputs, true))
            return false;

        return true;
    }

    bool LocalPrivateKeyKeeper2::Aggregation::Aggregate(const std::vector<CoinID>& v, bool bOuts)
    {
        Values& vals = bOuts ? m_Outs : m_Ins;
        ZeroObject(vals);

        Scalar::Native sk;
        for (size_t i = 0; i < v.size(); i++)
        {
            const CoinID& cid = v[i];

            CoinID::Worker(cid).Create(sk, *cid.get_ChildKdf(m_This.m_pKdf));
            m_sk += sk;

            if (m_NonConventional)
                continue; // ignore values

            if (cid.get_Scheme() < CoinID::Scheme::V1)
            {
                // disallow weak scheme
                if (bOuts)
                    return false; // no reason to create new weak outputs

                if (m_This.IsTrustless())
                    return false;
            }

            bool bAdded = false;
            if (cid.m_AssetID)
            {
                if (m_AssetID)
                {
                    if (m_AssetID != cid.m_AssetID)
                        return false; // mixed assets are not allowed in a single tx
                }
                else
                    m_AssetID = cid.m_AssetID;

                bAdded = Add(vals.m_Asset, cid.m_Value);
            }
            else
                bAdded = Add(vals.m_Beam, cid.m_Value);

            if (!bAdded)
                return false; // overflow
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
        if (IsTrustless())
        {
            // disallow weak paramters
            if (x.m_hScheme < Rules::get().pForks[1].m_Height)
                return Status::Unspecified; // blinding factor can be tampered without user permission

            if (x.m_Cid.get_Scheme() < CoinID::Scheme::V1)
                return Status::UserAbort; // value can be tampered without user permission
        }

        x.m_pResult.reset(new Output);

        Scalar::Native sk;
        x.m_pResult->Create(x.m_hScheme, sk, *x.m_Cid.get_ChildKdf(m_pKdf), x.m_Cid, *m_pKdf);

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
        Aggregation aggr(*this);
        if (!aggr.Aggregate(x))
            return Status::Unspecified;

        Aggregation::Values& vals = aggr.m_Outs; // alias

        if (!x.m_NonConventional)
        {
            if (!vals.Subtract(aggr.m_Ins))
                return Status::Unspecified; // not receiving

            if (vals.m_Beam)
            {
                if (aggr.m_AssetID)
                    return Status::Unspecified; // can't receive both

                vals.m_Asset = vals.m_Beam;
            }
            else
            {
                if (!vals.m_Asset)
                    return Status::Unspecified; // should receive at least something
            }
        }

        assert(x.m_pKernel);
        TxKernelStd& krn = *x.m_pKernel;

        Scalar::Native kKrn, kNonce;

        // Hash *ALL* the parameters, make the context unique
        krn.UpdateID();
        Hash::Value& hv = krn.m_Internal.m_ID; // alias

        Hash::Processor()
            << krn.m_Internal.m_ID
            << krn.m_Signature.m_NoncePub
            << x.m_NonConventional
            << x.m_Peer
            << x.m_MyIDKey
            << aggr.m_sk
            << vals.m_Asset
            << aggr.m_AssetID
            >> hv;

        NonceGenerator ng("hw-wlt-rcv");
        ng << hv;
        ng >> kKrn;
        ng >> kNonce;

        Point::Native commitment;
        if (!commitment.Import(krn.m_Commitment))
            return Status::Unspecified;

        Point::Native temp;
        temp = Context::get().G * kKrn; // public kernel commitment
        commitment += temp;
        krn.m_Commitment = commitment;

        if (!commitment.Import(krn.m_Signature.m_NoncePub))
            return Status::Unspecified;

        temp = Context::get().G * kNonce;
        commitment += temp;
        krn.m_Signature.m_NoncePub = commitment;

        krn.UpdateID();

        krn.m_Signature.SignPartial(hv, kKrn, kNonce);
        UpdateOffset(x, aggr.m_sk, kKrn);

        if (x.m_MyIDKey && !x.m_NonConventional)
        {
            PaymentConfirmation pc;
            pc.m_KernelID = hv;
            pc.m_Sender = x.m_Peer;
            pc.m_Value = vals.m_Asset;
            pc.m_AssetID = aggr.m_AssetID;

            m_pKdf->DeriveKey(kKrn, Key::ID(x.m_MyIDKey, Key::Type::WalletID));

            PeerID wid;
            wid.FromSk(kKrn);
            pc.Sign(kKrn);

            x.m_PaymentProofSignature = pc.m_Signature;
        }

        return Status::Success;
    }

    IPrivateKeyKeeper2::Status::Type LocalPrivateKeyKeeper2::InvokeSync(Method::SignSender& x)
    {
        Aggregation aggr(*this);
        if (!aggr.Aggregate(x))
            return Status::Unspecified;

        assert(x.m_pKernel);
        TxKernelStd& krn = *x.m_pKernel;

        Aggregation::Values& vals = aggr.m_Ins; // alias

        if (!x.m_NonConventional)
        {
            if (x.m_Peer == Zero)
                return Status::UserAbort; // conventional transfers must always be signed

            if (!vals.Subtract(aggr.m_Outs))
                return Status::Unspecified; // not sending

            if (aggr.m_AssetID)
            {
                if (!vals.m_Asset)
                    return Status::Unspecified; // asset involved, but no net transfer

                if (vals.m_Beam != krn.m_Fee)
                    return Status::Unspecified; // all beams must be consumed by the fee
            }
            else
            {
                if (vals.m_Beam <= krn.m_Fee)
                    return Status::Unspecified;

                vals.m_Asset = vals.m_Beam - krn.m_Fee;
            }
        }

        if (x.m_Slot >= get_NumSlots())
            return Status::Unspecified;

        Scalar::Native kNonce;

        if (x.m_MyIDKey)
        {
            m_pKdf->DeriveKey(kNonce, Key::ID(x.m_MyIDKey, Key::Type::WalletID));
            x.m_MyID.FromSk(kNonce);
        }
        else
        {
            // legacy. We need to verify the payment proof vs externally-specified our ID (usually SBBS address)
            if (IsTrustless())
                return false;
        }

        get_Nonce(kNonce, x.m_Slot);

        // during negotiation kernel height and commitment are adjusted. We should only commit to the Fee
        Hash::Value& hv = krn.m_Internal.m_ID; // alias. Just reuse this variable
        Hash::Processor()
            << krn.m_Fee
            << x.m_Peer
            << x.m_MyID
            << x.m_NonConventional
            << aggr.m_sk
            << vals.m_Asset
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
                Status::Type res = ConfirmSpend(vals.m_Asset, aggr.m_AssetID, x.m_Peer, krn, false);
                if (Status::Success != res)
                    return res;
            }

            x.m_UserAgreement = hv;

            Point::Native commitment = Context::get().G * kKrn;
            krn.m_Commitment = commitment;

            commitment = Context::get().G * kNonce;
            krn.m_Signature.m_NoncePub = commitment;

            return Status::Success;
        }

        if (x.m_UserAgreement != hv)
            return Status::Unspecified; // incorrect user agreement token

        krn.UpdateID();

        if ((x.m_Peer != Zero) && !x.m_NonConventional)
        {
            PaymentConfirmation pc;
            pc.m_KernelID = krn.m_Internal.m_ID;
            pc.m_Value = vals.m_Asset;
            pc.m_AssetID = aggr.m_AssetID;
            pc.m_Sender = x.m_MyID;
            pc.m_Signature = x.m_PaymentProofSignature;
            if (!pc.IsValid(x.m_Peer))
                return Status::Unspecified;
        }

        if (IsTrustless())
        {
            // 2nd user confirmation request. Now the kernel is complete, its ID can be calculated
            Status::Type res = ConfirmSpend(vals.m_Asset, aggr.m_AssetID, x.m_Peer, krn, true);
            if (Status::Success != res)
                return res;
        }

        Regenerate(x.m_Slot);

        Scalar::Native kSig = krn.m_Signature.m_k;
        krn.m_Signature.SignPartial(hv, kKrn, kNonce);
        kSig += krn.m_Signature.m_k;
        krn.m_Signature.m_k = kSig;

        UpdateOffset(x, aggr.m_sk, kKrn);

        return Status::Success;
    }

    IPrivateKeyKeeper2::Status::Type LocalPrivateKeyKeeper2::InvokeSync(Method::SignSplit& x)
    {
        Aggregation aggr(*this);
        if (!aggr.Aggregate(x))
            return Status::Unspecified;

        Aggregation::Values& vals = aggr.m_Ins; // alias
        if (!vals.Subtract(aggr.m_Outs))
            return Status::Unspecified; // not spending

        assert(x.m_pKernel);
        TxKernelStd& krn = *x.m_pKernel;

        if (vals.m_Asset || vals.m_Beam != krn.m_Fee)
            return Status::Unspecified; // some funds are missing!

        Scalar::Native kKrn, kNonce;

        Hash::Value& hv = krn.m_Internal.m_ID; // alias

        Hash::Processor()
            << krn.m_Height.m_Min
            << krn.m_Height.m_Max
            << krn.m_Fee
            << aggr.m_sk
            >> hv;

        NonceGenerator ng("hw-wlt-split");
        ng << hv;
        ng >> kKrn;
        ng >> kNonce;

        Point::Native commitment = Context::get().G * kKrn; // public kernel commitment
        krn.m_Commitment = commitment;

        commitment = Context::get().G * kNonce;
        krn.m_Signature.m_NoncePub = commitment;

        krn.UpdateID();

        Status::Type res = ConfirmSpend(0, 0, Zero, krn, true);
        if (Status::Success != res)
            return res;

        krn.m_Signature.SignPartial(hv, kKrn, kNonce);
        UpdateOffset(x, aggr.m_sk, kKrn);

        return Status::Success;
    }

    /////////////////////////
    // LocalPrivateKeyKeeperStd
    void LocalPrivateKeyKeeperStd::State::Generate()
    {
        for (Slot::Type i = 0; i < s_Slots; i++)
            Regenerate(i);
    }

    IPrivateKeyKeeper2::Slot::Type LocalPrivateKeyKeeperStd::get_NumSlots()
    {
        return s_Slots;
    }

    void LocalPrivateKeyKeeperStd::get_Nonce(ECC::Scalar::Native& ret, Slot::Type iSlot)
    {
        assert(iSlot < s_Slots);
        m_pKdf->DeriveKey(ret, m_State.m_pSlot[iSlot]);
    }

    void LocalPrivateKeyKeeperStd::State::Regenerate(Slot::Type iSlot)
    {
        assert(iSlot < s_Slots);
        Hash::Processor() << m_hvLast >> m_hvLast;
        m_pSlot[iSlot] = m_hvLast;
    }

    void LocalPrivateKeyKeeperStd::Regenerate(Slot::Type iSlot)
    {
        m_State.Regenerate(iSlot);
    }

} // namespace beam::wallet
