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


    void LocalPrivateKeyKeeper::GeneratePublicKeys(const vector<Key::IDV>& ids, bool createCoinKey, Callback<PublicKeys>&& resultCallback, ExceptionCallback&& exceptionCallback)
    {
        DoAsync([=]() { return GeneratePublicKeysSync(ids, createCoinKey); }, move(resultCallback), move(exceptionCallback));
    }

    void LocalPrivateKeyKeeper::GeneratePublicKeysEx(const std::vector<Key::IDV>& ids, bool createCoinKey, Asset::ID assetID, Callback<PublicKeys>&& resultCallback, ExceptionCallback&& exceptionCallback)
    {
        DoAsync([=]() { return GeneratePublicKeysSyncEx(ids, createCoinKey, assetID); }, move(resultCallback), move(exceptionCallback));
    }

    void LocalPrivateKeyKeeper::GenerateOutputs(Height schemeHeight, const std::vector<Key::IDV>& ids, Callback<Outputs>&& resultCallback, ExceptionCallback&& exceptionCallback)
    {
        DoThreadAsync([=]() { return GenerateOutputsSync(schemeHeight, ids); }, std::move(resultCallback), std::move(exceptionCallback));
    }

    void LocalPrivateKeyKeeper::GenerateOutputsEx(Height schemeHeight, const std::vector<Key::IDV>& ids, Asset::ID assetId, Callback<Outputs>&& resultCallback, ExceptionCallback&& exceptionCallback)
    {
        DoThreadAsync([=]() { return GenerateOutputsSyncEx(schemeHeight, ids, assetId); }, std::move(resultCallback), std::move(exceptionCallback));
    }

    void LocalPrivateKeyKeeper::SignReceiver(const std::vector<Key::IDV>& inputs
                                           , const std::vector<Key::IDV>& outputs
                                           , Asset::ID assetId
                                           , const KernelParameters& kernelParameters
                                           , const WalletIDKey& walletIDkey
                                           , Callback<ReceiverSignature>&& resultCallback
                                           , ExceptionCallback&& exceptionCallback)
    {
        DoAsync([=]() { return SignReceiverSync(inputs, outputs, assetId, kernelParameters, walletIDkey); }, move(resultCallback), move(exceptionCallback));
    }

    void LocalPrivateKeyKeeper::SignSender(const std::vector<Key::IDV>& inputs
                                         , const std::vector<Key::IDV>& outputs
                                         , Asset::ID assetId
                                         , size_t nonceSlot
                                         , const KernelParameters& kernelParameters
                                         , bool initial
                                         , Callback<SenderSignature>&& resultCallback
                                         , ExceptionCallback&& exceptionCallback)
    {
        DoAsync([=]() { return SignSenderSync(inputs, outputs, assetId, nonceSlot, kernelParameters, initial); }, move(resultCallback), move(exceptionCallback));
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

    IPrivateKeyKeeper::PublicKeys LocalPrivateKeyKeeper::GeneratePublicKeysSync(const std::vector<Key::IDV>& ids, bool createCoinKey)
    {
        PublicKeys result;
        Scalar::Native secretKey;
        result.reserve(ids.size());
        if (createCoinKey)
        {
            for (const auto& coinID : ids)
            {
                Point& publicKey = result.emplace_back();
                SwitchCommitment().Create(secretKey, publicKey, *GetChildKdf(coinID), coinID);
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

    IPrivateKeyKeeper::PublicKeys LocalPrivateKeyKeeper::GeneratePublicKeysSyncEx(const std::vector<Key::IDV>& ids, bool createCoinKey, Asset::ID assetId)
    {
        PublicKeys resKeys;
        Scalar::Native secretKey;
        resKeys.reserve(ids.size());
        if (createCoinKey)
        {
            for (const auto& coinID : ids)
            {
                if(coinID.isAsset())
                {
                    Point &publicKey = resKeys.emplace_back();
                    SwitchCommitment(assetId).Create(secretKey, publicKey, *GetChildKdf(coinID), coinID);
                }
                else
                {
                    Point &publicKey = resKeys.emplace_back();
                    SwitchCommitment().Create(secretKey, publicKey, *GetChildKdf(coinID), coinID);
                }
            }
        }
        else
        {
            for (const auto& keyID : ids)
            {
                assert(!keyID.isAsset()); // TODO:ASSET
                Point& publicKey = resKeys.emplace_back();
                m_MasterKdf->DeriveKey(secretKey, keyID);
                publicKey = Context::get().G * secretKey;
            }
        }
        return std::move(resKeys);
    }

    ECC::Point LocalPrivateKeyKeeper::GeneratePublicKeySync(const Key::IDV& id)
    {
        Scalar::Native secretKey;
        Point publicKey;

        m_MasterKdf->DeriveKey(secretKey, id);
        publicKey = Context::get().G * secretKey;

        return publicKey;
    }

    ECC::Point LocalPrivateKeyKeeper::GenerateCoinKeySync(const Key::IDV& id, Asset::ID assetId)
    {
        Scalar::Native secretKey;
        Point publicKey;
        SwitchCommitment(assetId).Create(secretKey, publicKey, *GetChildKdf(id), id);
        return publicKey;
    }

    IPrivateKeyKeeper::Outputs LocalPrivateKeyKeeper::GenerateOutputsSync(Height schemeHeigh, const std::vector<Key::IDV>& ids)
    {
        Outputs result;
        Scalar::Native secretKey;
        Point commitment;
        result.reserve(ids.size());
        for (const auto& coinID : ids)
        {
            auto& output = result.emplace_back(make_unique<Output>());
            output->Create(schemeHeigh, secretKey, *GetChildKdf(coinID), coinID, *m_MasterKdf);
        }
        return result;
    }

    IPrivateKeyKeeper::Outputs LocalPrivateKeyKeeper::GenerateOutputsSyncEx(Height schemeHeigh, const std::vector<Key::IDV>& ids, Asset::ID assetId)
    {
        Outputs resOuts;
        Scalar::Native secretKey;
        Point commitment;
        resOuts.reserve(ids.size());

        for (const auto& coinID : ids)
        {
            auto& output = resOuts.emplace_back(make_unique<Output>());
            output->Create(schemeHeigh, secretKey, *GetChildKdf(coinID), CoinID(coinID, assetId), *m_MasterKdf);
        }

        return std::move(resOuts);
    }

    ECC::Point LocalPrivateKeyKeeper::GenerateNonceSync(size_t slot)
    {
        Point::Native result = Context::get().G * GetNonce(slot);
        return result;
    }

    Scalar LocalPrivateKeyKeeper::SignSync(const std::vector<Key::IDV>& inputs, const std::vector<Key::IDV>& outputs, Asset::ID assetId, const Scalar::Native& offset, size_t nonceSlot, const KernelParameters& kernelParameters, const Point::Native& publicNonce)
    {
        auto excess = GetExcess(inputs, outputs, assetId, offset);

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

        Scalar::Native nonce = GetNonce(nonceSlot);

        // TODO: Fix this!!!
        // If the following line is uncommented - swap_test hangs!
        //ECC::GenRandom(m_Nonces[nonceSlot].V); // Invalidate slot immediately after using it (to make it similar to HW wallet)!

		kernel.m_Signature.m_NoncePub = publicNonce;
		kernel.m_Signature.SignPartial(message, excess, nonce);

		return kernel.m_Signature.m_k;
    }

    ReceiverSignature LocalPrivateKeyKeeper::SignReceiverSync(const std::vector<Key::IDV>& inputs
                                                        , const std::vector<Key::IDV>& outputs
                                                        , Asset::ID assetID
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
       
        auto excess = GetExcess(inputs, outputs, assetID, Zero);
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

    SenderSignature LocalPrivateKeyKeeper::SignSenderSync(const std::vector<Key::IDV>& inputs
                                                    , const std::vector<Key::IDV>& outputs
                                                    , Asset::ID assetID
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

        auto excess = GetExcess(inputs, outputs, assetID, Zero);

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

    Key::IKdf::Ptr LocalPrivateKeyKeeper::GetChildKdf(const Key::IDV& kidv) const
    {
        return MasterKey::get_Child(m_MasterKdf, kidv);
    }

    Scalar::Native LocalPrivateKeyKeeper::GetNonce(size_t slot)
    {
        const auto& randomValue = m_Nonces[slot].V;

        Scalar::Native nonce;
        m_MasterKdf->DeriveKey(nonce, randomValue);

        return nonce;
    }

    Scalar::Native LocalPrivateKeyKeeper::GetExcess(const std::vector<Key::IDV>& inputs, const std::vector<Key::IDV>& outputs, Asset::ID assetId, const ECC::Scalar::Native& offset) const
    {
        // Excess = Sum(input blinfing factors) - Sum(output blinfing factors) - offset
        Point commitment;
        Scalar::Native blindingFactor;
        Scalar::Native excess = offset;

        for (const auto& coinID : outputs)
        {
            SwitchCommitment(coinID.isAsset() ? assetId : 0).Create(blindingFactor, commitment, *GetChildKdf(coinID), coinID);
            excess += blindingFactor;
        }
        excess = -excess;
        for (const auto& coinID : inputs)
        {
            SwitchCommitment(coinID.isAsset() ? assetId : 0).Create(blindingFactor, commitment, *GetChildKdf(coinID), coinID);
            excess += blindingFactor;
        }

        return excess;
    }

    int64_t LocalPrivateKeyKeeper::CalculateValue(const std::vector<Key::IDV>& inputs, const std::vector<Key::IDV>& outputs) const
    {
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

    ECC::Scalar::Native LocalPrivateKeyKeeper::SignEmissionKernel(TxKernelAssetEmit& kernel, Key::Index assetOwnerIdx)
    {
        ECC::Scalar::Native kernelSk;
        m_MasterKdf->DeriveKey(kernelSk, Key::ID(assetOwnerIdx, Key::Type::Kernel, assetOwnerIdx));
        kernel.Sign(kernelSk, GetAssetOwnerKeypair(assetOwnerIdx).second);
        return -kernelSk;
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
}