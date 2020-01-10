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
       // DoAsync(MakeAsyncFunc(&LocalPrivateKeyKeeper::GeneratePublicKeysSync, ids, createCoinKey), move(resultCallback), move(exceptionCallback));

        DoAsync([this, ids, createCoinKey]() { return GeneratePublicKeysSync(ids, createCoinKey); }, move(resultCallback), move(exceptionCallback));
    }

    void LocalPrivateKeyKeeper::GenerateOutputs(Height schemeHeight, const std::vector<Key::IDV>& ids, Callback<Outputs>&& resultCallback, ExceptionCallback&& exceptionCallback)
    {
        DoThreadAsync([this, schemeHeight, ids]() { return GenerateOutputsSync(schemeHeight, ids); }, std::move(resultCallback), std::move(exceptionCallback));
    }

    void LocalPrivateKeyKeeper::GenerateOutputsEx(Height schemeHeight, const std::vector<Key::IDV>& ids, const AssetID& assetId, CallbackEx<Outputs, ECC::Scalar::Native>&& resultCallback, ExceptionCallback&& exceptionCallback)
     {
        auto thisHolder = shared_from_this();
        auto resOuts    = make_shared<Outputs>();
        auto resOffset  = make_shared<ECC::Scalar::Native>();
        shared_ptr<exception> storedException;
        shared_ptr<future<void>> futureHolder = make_shared<future<void>>();
        *futureHolder = do_thread_async(
            [thisHolder, this, schemeHeight, ids, assetId, resOuts, resOffset, storedException]()
            {
                try
                {
                   std::tie(*resOuts, *resOffset) = GenerateOutputsSyncEx(schemeHeight, ids, assetId);
                }
                catch (const exception& ex)
                {
                    *storedException = ex;
                }
            },
            [futureHolder, resultCallback = move(resultCallback), exceptionCallback = move(exceptionCallback), resOuts, resOffset, storedException]() mutable
            {
                if (storedException)
                {
                    exceptionCallback(*storedException);
                }
                else
                {
                    resultCallback(move(*resOuts), std::move(*resOffset));
                }
                futureHolder.reset();
            });
     }

    size_t LocalPrivateKeyKeeper::AllocateNonceSlot()
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

    std::pair<IPrivateKeyKeeper::PublicKeys, ECC::Scalar::Native> LocalPrivateKeyKeeper::GeneratePublicKeysSyncEx(const std::vector<Key::IDV>& ids, bool createCoinKey, const AssetID& assetID)
    {
        PublicKeys resKeys;
        Scalar::Native resOffset;
        Scalar::Native secretKey;
        resKeys.reserve(ids.size());
        if (createCoinKey)
        {
            for (const auto& coinID : ids)
            {
                if(coinID.isAsset())
                {
                    Point &publicKey = resKeys.emplace_back();
                    SwitchCommitment(&assetID).Create(secretKey, publicKey, *GetChildKdf(coinID), coinID);
                    resOffset += secretKey;
                }
                else
                {
                    Point &publicKey = resKeys.emplace_back();
                    SwitchCommitment().Create(secretKey, publicKey, *GetChildKdf(coinID), coinID);
                    resOffset += secretKey;
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
                resOffset += secretKey;
            }
        }
        return std::make_pair(std::move(resKeys), std::move(resOffset));
    }

    ECC::Point LocalPrivateKeyKeeper::GeneratePublicKeySync(const Key::IDV& id)
    {
        Scalar::Native secretKey;
        Point publicKey;

        m_MasterKdf->DeriveKey(secretKey, id);
        publicKey = Context::get().G * secretKey;

        return publicKey;
    }

    ECC::Point LocalPrivateKeyKeeper::GenerateCoinKeySync(const Key::IDV& id, const AssetID& assetId)
    {
        Scalar::Native secretKey;
        Point publicKey;
        SwitchCommitment(&assetId).Create(secretKey, publicKey, *GetChildKdf(id), id);
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

    std::pair<IPrivateKeyKeeper::Outputs, ECC::Scalar::Native> LocalPrivateKeyKeeper::GenerateOutputsSyncEx(Height schemeHeigh, const std::vector<Key::IDV>& ids, const AssetID& assetId)
    {
        Outputs resOuts;
        Scalar::Native resOffset;
        Scalar::Native secretKey;
        Point commitment;
        resOuts.reserve(ids.size());

        for (const auto& coinID : ids)
        {
            auto& output = resOuts.emplace_back(make_unique<Output>());
            if (coinID.isAsset())
            {
                output->m_AssetID = assetId;
            }
            output->Create(schemeHeigh, secretKey, *GetChildKdf(coinID), coinID, *m_MasterKdf);
            resOffset += -secretKey;
        }

        return std::make_pair(std::move(resOuts), std::move(resOffset));
    }

    ECC::Point LocalPrivateKeyKeeper::GenerateNonceSync(size_t slot)
    {
        Point::Native result = Context::get().G * GetNonce(slot);
        return result;
    }

    Scalar LocalPrivateKeyKeeper::SignSync(const std::vector<Key::IDV>& inputs, const std::vector<Key::IDV>& outputs, const AssetID& assetId, const Scalar::Native& offset, size_t nonceSlot, const KernelParameters& kernelParamerters, const Point::Native& publicNonce)
    {
        auto excess = GetExcess(inputs, outputs, assetId, offset);

        TxKernelStd kernel;
        kernel.m_Commitment = kernelParamerters.commitment;
        kernel.m_Fee = kernelParamerters.fee;
        kernel.m_Height = kernelParamerters.height;
        if (kernelParamerters.lockImage || kernelParamerters.lockPreImage)
        {
            kernel.m_pHashLock = make_unique<TxKernelStd::HashLock>();

			if (kernelParamerters.lockPreImage)
				kernel.m_pHashLock->m_Value = *kernelParamerters.lockPreImage;
			else
			{
				kernel.m_pHashLock->m_Value = *kernelParamerters.lockImage;
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
                                                        , const AssetID& assetId
                                                        , const KernelParameters& kernelParamerters
                                                        , const ECC::Point& publicNonce
                                                        , const PeerID& peerID
                                                        , const WalletIDKey& walletIDkey)
    {
        ReceiverSignature res;
        auto value = CalculateValue(inputs, outputs);
        if (value > 0 && !inputs.empty() && 
            (Amount(value) == kernelParamerters.fee // self tx
                || outputs.empty())) // spending shared utxo
        {
            value = 0;
        }
        else if (value >= 0)
        {
            throw KeyKeeperException("Receover failed to sign tx. We are not receiving");
        }
       
        auto excess = GetExcess(inputs, outputs, assetId, Zero);
        Amount val = -value;

        Scalar::Native kKrn, kNonce;
        
        ECC::Hash::Value hv;
        Hash::Processor()
            << kernelParamerters.fee
            << kernelParamerters.height.m_Min
            << kernelParamerters.height.m_Max
            << kernelParamerters.commitment
            << publicNonce
            << kernelParamerters.peerID
            << excess
            << val >> hv;

        NonceGenerator ng("hw-wlt-rcv");
        ng << hv;

        ng >> kKrn;
        ng >> kNonce; 

        Point::Native commitment;
        if (!commitment.Import(kernelParamerters.commitment))
        {
            throw InvalidParametersException();
        }
        
        Point::Native temp;
        temp = Context::get().G * kKrn; // public kernel commitment
        commitment += temp;
        

        TxKernelStd kernel;
        kernel.m_Commitment = commitment;
        kernel.m_Fee = kernelParamerters.fee;
        kernel.m_Height = kernelParamerters.height;
        if (kernelParamerters.lockImage || kernelParamerters.lockPreImage)
        {
            kernel.m_pHashLock = make_unique<TxKernelStd::HashLock>();
        
            if (kernelParamerters.lockPreImage)
                kernel.m_pHashLock->m_Value = *kernelParamerters.lockPreImage;
            else
            {
                kernel.m_pHashLock->m_Value = *kernelParamerters.lockImage;
                kernel.m_pHashLock->m_IsImage = true;
            }
        }
        kernel.UpdateID();
        const Merkle::Hash& message = kernel.m_Internal.m_ID;
        temp = Context::get().G * kNonce; // public receiver nonce, we don't need slots here since we sign transaction only once
        Point::Native pt;
        if (!pt.Import(publicNonce))
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
            pc.m_Sender = peerID;
            pc.m_Value = val;

            auto keyPair = GetWalletID(walletIDkey);

            pc.Sign(keyPair.m_PrivateKey);

            res.m_PaymentProofSignature = pc.m_Signature;
        }
        
        return res;
    }

    SenderSignature LocalPrivateKeyKeeper::SignSenderSync(const std::vector<Key::IDV>& inputs
                                                    , const std::vector<Key::IDV>& outputs
                                                    , const AssetID& assetId
                                                    , size_t nonceSlot
                                                    , const KernelParameters& kernelParamerters
                                                    , const ECC::Point& publicNonce
                                                    , bool initial)
    {
        SenderSignature res;
        auto value = CalculateValue(inputs, outputs);

        value -= kernelParamerters.fee;

        if (value < 0)
        {
            throw KeyKeeperException("Sender failed to sign tx. We are not sending");
        }

        auto excess = GetExcess(inputs, outputs, assetId, Zero);

        Scalar::Native kKrn;
        ECC::Hash::Value hv;
        Hash::Processor()
            << kernelParamerters.fee
            << kernelParamerters.height.m_Min
            // << kernelParamerters.height.m_Max
            //<< kernelParamerters.peerID 
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
        kernel.m_Commitment = kernelParamerters.commitment;
        kernel.m_Fee = kernelParamerters.fee;
        kernel.m_Height = kernelParamerters.height;
        if (kernelParamerters.lockImage || kernelParamerters.lockPreImage)
        {
            kernel.m_pHashLock = make_unique<TxKernelStd::HashLock>();

            if (kernelParamerters.lockPreImage)
                kernel.m_pHashLock->m_Value = *kernelParamerters.lockPreImage;
            else
            {
                kernel.m_pHashLock->m_Value = *kernelParamerters.lockImage;
                kernel.m_pHashLock->m_IsImage = true;
            }
        }
        kernel.UpdateID();
        const Merkle::Hash& message = kernel.m_Internal.m_ID;

        // TODO: temporal solution
        if (kernelParamerters.myID != Zero && kernelParamerters.peerID != Zero)
        {
            ////////////////////////
            // Verify peer signature
            PaymentConfirmation pc;
            pc.m_KernelID = message;
            pc.m_Value = Amount(value);
            pc.m_Sender = kernelParamerters.myID;
            pc.m_Signature = kernelParamerters.paymentProofSignature;

            if (!pc.IsValid(kernelParamerters.peerID))
            {
                throw InvalidPaymentProofException();
            }
        }
        
        /////////////////////////
        // Ask for user permission!
        //
        // ...

        if (!commitment.Import(kernelParamerters.commitment))
        {
            throw InvalidParametersException();
        }

        ECC::GenRandom(m_Nonces[nonceSlot].V); // Invalidate slot immediately after using it (to make it similar to HW wallet)!

        res.m_KernelSignature.m_NoncePub = publicNonce;// + myPublicNonce;
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

    Scalar::Native LocalPrivateKeyKeeper::GetExcess(const std::vector<Key::IDV>& inputs, const std::vector<Key::IDV>& outputs, const AssetID& assetId, const ECC::Scalar::Native& offset) const
    {
        // Excess = Sum(input blinfing factors) - Sum(output blinfing factors) - offset
        Point commitment;
        Scalar::Native blindingFactor;
        Scalar::Native excess = offset;

        for (const auto& coinID : outputs)
        {
            SwitchCommitment(coinID.isAsset() ? &assetId : nullptr).Create(blindingFactor, commitment, *GetChildKdf(coinID), coinID);
            excess += blindingFactor;
        }
        excess = -excess;
        for (const auto& coinID : inputs)
        {
            SwitchCommitment(coinID.isAsset() ? &assetId : nullptr).Create(blindingFactor, commitment, *GetChildKdf(coinID), coinID);
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

    ECC::Scalar::Native LocalPrivateKeyKeeper::SignEmissionKernel(TxKernelAssetEmit& kernel, uint32_t assetIdx)
    {
        ECC::Scalar::Native kernelSk;
        m_MasterKdf->DeriveKey(kernelSk, Key::ID(assetIdx, Key::Type::Kernel, assetIdx));
        kernel.Sign(kernelSk, GetAssetKeypair(assetIdx).second);
        return -kernelSk;
    }

    std::pair<AssetID, ECC::Scalar::Native> LocalPrivateKeyKeeper::GetAssetKeypair(uint32_t assetIdx)
    {
        Scalar::Native skAssetSk;
        m_MasterKdf->DeriveKey(skAssetSk, beam::Key::ID(assetIdx, beam::Key::Type::Asset));

        beam::AssetID assetId;
        beam::proto::Sk2Pk(assetId, skAssetSk);

        return std::make_pair(assetId, std::move(skAssetSk));
    }

    AssetID LocalPrivateKeyKeeper::GetAssetID(uint32_t assetIdx)
    {
        return GetAssetKeypair(assetIdx).first;
    }
}