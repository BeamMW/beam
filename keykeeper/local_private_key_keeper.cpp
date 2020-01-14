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
        try
        {
            resultCallback(GeneratePublicKeysSync(ids, createCoinKey));
        }
        catch (const exception& ex)
        {
            exceptionCallback(ex);
        }
    }

    void LocalPrivateKeyKeeper::GenerateOutputs(Height schemeHeight, const std::vector<Key::IDV>& ids, Callback<Outputs>&& resultCallback, ExceptionCallback&& exceptionCallback)
    {
        auto thisHolder = shared_from_this();
        shared_ptr<Outputs> result = make_shared<Outputs>();
        shared_ptr<exception> storedException;
        shared_ptr<future<void>> futureHolder = make_shared<future<void>>();
        *futureHolder = do_thread_async(
            [thisHolder, this, schemeHeight, ids, result, storedException]()
            {
                try
                {
                    *result = GenerateOutputsSync(schemeHeight, ids);
                }
                catch (const exception& ex)
                {
                    *storedException = ex;
                }
            },
            [futureHolder, resultCallback = move(resultCallback), exceptionCallback = move(exceptionCallback), result, storedException]() mutable
            {
                if (storedException)
                {
                    exceptionCallback(*storedException);
                }
                else
                {
                    resultCallback(move(*result));
                }
                futureHolder.reset();
            });

    }

    void LocalPrivateKeyKeeper::GenerateOutputsEx(Height schemeHeight, const std::vector<Key::IDV>& ids, AssetID assetId, CallbackEx<Outputs, ECC::Scalar::Native>&& resultCallback, ExceptionCallback&& exceptionCallback)
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

    std::pair<IPrivateKeyKeeper::PublicKeys, ECC::Scalar::Native> LocalPrivateKeyKeeper::GeneratePublicKeysSyncEx(const std::vector<Key::IDV>& ids, bool createCoinKey, AssetID assetId)
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
                    SwitchCommitment(assetId).Create(secretKey, publicKey, *GetChildKdf(coinID), coinID);
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

    ECC::Point LocalPrivateKeyKeeper::GenerateCoinKeySync(const Key::IDV& id, AssetID assetId)
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

    std::pair<IPrivateKeyKeeper::Outputs, ECC::Scalar::Native> LocalPrivateKeyKeeper::GenerateOutputsSyncEx(Height schemeHeigh, const std::vector<Key::IDV>& ids, AssetID assetId)
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

    Scalar LocalPrivateKeyKeeper::SignSync(const std::vector<Key::IDV>& inputs, const std::vector<Key::IDV>& outputs, AssetID assetId, const Scalar::Native& offset, size_t nonceSlot, const KernelParameters& kernelParamerters, const Point::Native& publicNonce)
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

    boost::optional<ReceiverSignature> LocalPrivateKeyKeeper::SignReceiver(const std::vector<Key::IDV>& inputs, const std::vector<Key::IDV>& outputs, AssetID assetId, const KernelParameters& kernelParamerters, const ECC::Point& publicNonce)
    {
        boost::optional<ReceiverSignature> res;
        auto value = CalculateValue(inputs, outputs);
        if (value >= 0)
        {
            return res; // we are not receiving
        }

        auto excess = GetExcess(inputs, outputs, assetId, Zero);
        Amount val = -value;

        Scalar::Native kKrn, kNonce;
        Oracle ng;
        ng
            << "hw-wlt-rcv"
            << kernelParamerters.fee
            << kernelParamerters.height.m_Min
            << kernelParamerters.height.m_Max
            << kernelParamerters.commitment
            << publicNonce
//            << kernelParamerters.m_Peer
            << excess
            << val;

        ng >> kKrn;
        ng >> kNonce;

        Point::Native commitment;
        if (!commitment.Import(kernelParamerters.commitment))
        {
            return res;
        }
        
        Point::Native temp;
        temp = Context::get().G * kKrn; // public kernel commitment
        commitment += temp;
        
        // 
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
            return res;
        }
        res.emplace();
        res->m_KernelCommitment = kernel.m_Commitment;
        res->m_KernelSignature.m_NoncePub = pt + temp;
        res->m_KernelSignature.SignPartial(message, kKrn, kNonce);
        kKrn = -kKrn;
        excess += kKrn;
        res->m_Offset = excess;

        //PaymentConfirmation pc;
        //pc.m_KernelID = kernel.m_Internal.m_ID;
        //pc.m_Sender = tx.m_Peer;
        //pc.m_Value = val;
        //
        //beam::PeerID pidSelf;
        //GetWalletIDInternal(pidSelf, skTotal);
        //pc.Sign(tx.m_PaymentProofSignature, skTotal);

        return res;
    }

    boost::optional<SenderSignature> LocalPrivateKeyKeeper::SignSender(const std::vector<Key::IDV>& inputs, const std::vector<Key::IDV>& outputs, AssetID assetId, size_t nonceSlot, const KernelParameters& kernelParamerters, const ECC::Point& publicNonce, bool initial)
    {
        boost::optional<SenderSignature> res;
        auto value = CalculateValue(inputs, outputs);

        value -= kernelParamerters.fee;

        if (value <= 0)
        {
            return res; // we are not sending
        }

        auto excess = GetExcess(inputs, outputs, assetId, Zero);

        Scalar::Native kKrn;
        Oracle ng;
        ng
            << "hw-wlt-snd"
            << kernelParamerters.fee
            << kernelParamerters.height.m_Min
            << kernelParamerters.height.m_Max
            //            << kernelParamerters.m_Peer
            << excess
            << Amount(value);

        ng >> kKrn;

        Point::Native commitment;
        commitment = Context::get().G * kKrn; // public kernel commitment

        Scalar::Native nonce = GetNonce(nonceSlot);
        Point::Native myPublicNonce;
        myPublicNonce = Context::get().G * nonce;
        if (initial)
        {
            res.emplace();
            res->m_KernelCommitment = commitment;
            res->m_KernelSignature.m_NoncePub = myPublicNonce;
            return res;
        }

        //////////////////////////
        //// Verify peer signature
        //PaymentConfirmation pc;
        //pc.m_KernelID = krn.m_Internal.m_ID;
        //pc.m_Value = dVal;
        //
        //
        //Scalar::Native skId;
        //GetWalletIDInternal(pc.m_Sender, skId);
        //
        //if (!pc.IsValid(tx.m_PaymentProofSignature, tx.m_Peer))
        //    return false;
        //
        ///////////////////////////
        //// Ask for user permission!
        ////
        //// ...

        // 

        if (!commitment.Import(kernelParamerters.commitment))
        {
            return res;
        }

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


        // TODO: Fix this!!!
        // If the following line is uncommented - swap_test hangs!
        //ECC::GenRandom(m_Nonces[nonceSlot].V); // Invalidate slot immediately after using it (to make it similar to HW wallet)!

        //kernel.m_Signature.m_NoncePub = publicNonce;
        //kernel.m_Signature.SignPartial(message, kKrn, nonce);
        res.emplace();
        res->m_KernelSignature.m_NoncePub = publicNonce;// + myPublicNonce;
        res->m_KernelSignature.SignPartial(message, kKrn, nonce);

        kKrn = -kKrn;
        excess += kKrn;
        res->m_Offset = excess;

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

    Scalar::Native LocalPrivateKeyKeeper::GetExcess(const std::vector<Key::IDV>& inputs, const std::vector<Key::IDV>& outputs, AssetID assetId, const ECC::Scalar::Native& offset) const
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