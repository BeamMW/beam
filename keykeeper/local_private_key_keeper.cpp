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

    void LocalPrivateKeyKeeper::GenerateOutputsEx(Height schemeHeight, const std::vector<Key::IDV>& ids, const AssetID& assetId, ECC::Scalar::Native& offset, Callback<Outputs>&& resultCallback, ExceptionCallback&& exceptionCallback)
     {
        auto thisHolder = shared_from_this();
        shared_ptr<Outputs> result = make_shared<Outputs>();
        shared_ptr<exception> storedException;
        shared_ptr<future<void>> futureHolder = make_shared<future<void>>();
        *futureHolder = do_thread_async(
            [thisHolder, this, schemeHeight, ids, assetId, result, storedException, &offset]()
            {
                try
                {
                    *result = GenerateOutputsSyncEx(schemeHeight, ids, assetId, offset);
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

    IPrivateKeyKeeper::PublicKeys LocalPrivateKeyKeeper::GeneratePublicKeysSyncEx(const std::vector<Key::IDV>& ids, bool createCoinKey, const AssetID& assetID, Scalar::Native& offset)
    {
        PublicKeys result;
        Scalar::Native secretKey;
        result.reserve(ids.size());
        if (createCoinKey)
        {
            for (const auto& coinID : ids)
            {
                if(coinID.isAsset())
                {
                    Point &publicKey = result.emplace_back();
                    SwitchCommitment(&assetID).Create(secretKey, publicKey, *GetChildKdf(coinID), coinID);
                    offset += secretKey;
                }
                else
                {
                    Point &publicKey = result.emplace_back();
                    SwitchCommitment().Create(secretKey, publicKey, *GetChildKdf(coinID), coinID);
                    offset += secretKey;
                }
            }
        }
        else
        {
            for (const auto& keyID : ids)
            {
                assert(!keyID.isAsset()); // TODO:ASSET
                Point& publicKey = result.emplace_back();
                m_MasterKdf->DeriveKey(secretKey, keyID);
                publicKey = Context::get().G * secretKey;
                offset += secretKey;
            }
        }
        return result;
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

    IPrivateKeyKeeper::Outputs LocalPrivateKeyKeeper::GenerateOutputsSyncEx(Height schemeHeigh, const std::vector<Key::IDV>& ids, const AssetID& assetId, Scalar::Native& offset)
    {
        Outputs result;
        Scalar::Native secretKey;
        Point commitment;
        result.reserve(ids.size());
        for (const auto& coinID : ids)
        {
            auto& output = result.emplace_back(make_unique<Output>());
            if (coinID.isAsset())
            {
                output->m_AssetID = assetId;
            }
            output->Create(schemeHeigh, secretKey, *GetChildKdf(coinID), coinID, *m_MasterKdf);
            offset += -secretKey;
        }
        return result;
    }

    ECC::Point LocalPrivateKeyKeeper::GenerateNonceSync(size_t slot)
    {
        Point::Native result = Context::get().G * GetNonce(slot);
        return result;
    }

    Scalar LocalPrivateKeyKeeper::SignSync(const std::vector<Key::IDV>& inputs, const std::vector<Key::IDV>& outputs, const AssetID& assetId, const Scalar::Native& offset, size_t nonceSlot, const KernelParameters& kernelParamerters, const Point::Native& publicNonce)
    {
        auto excess = GetExcess(inputs, outputs, assetId, offset);

        TxKernel kernel;
        kernel.m_Commitment = kernelParamerters.commitment;
        kernel.m_Fee = kernelParamerters.fee;
        kernel.m_Height = kernelParamerters.height;
        if (kernelParamerters.hashLock)
        {
            kernel.m_pHashLock = make_unique<TxKernel::HashLock>(*kernelParamerters.hashLock);
        }
        Merkle::Hash message;
        kernel.get_Hash(message, kernelParamerters.lockImage ? &*kernelParamerters.lockImage : nullptr);

        ECC::Signature::MultiSig multiSig;
        ECC::Scalar::Native partialSignature;
        multiSig.m_NoncePub = publicNonce;
        multiSig.m_Nonce = GetNonce(nonceSlot);
        multiSig.SignPartial(partialSignature, message, excess);

        return Scalar(partialSignature);
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

        NoLeak<Scalar::Native> nonce;
        m_MasterKdf->DeriveKey(nonce.V, randomValue);
        return nonce.V;
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

    Scalar::Native LocalPrivateKeyKeeper::GetAssetKey(beam::Key::ID assetKeyId)
    {
        Scalar::Native secretKey;
        m_MasterKdf->DeriveKey(secretKey, assetKeyId);
        return secretKey;
    }

    AssetID LocalPrivateKeyKeeper::AIDFromKeyIndex(uint32_t assetIdx)
    {
        const auto assetKeyId = Key::ID(assetIdx, Key::Type::Asset, assetIdx);
        auto assetKey = GetAssetKey(assetKeyId);
        AssetID assetId = Zero;
        proto::Sk2Pk(assetId, assetKey);
        return assetId;
    }

    void LocalPrivateKeyKeeper::SignEmissionInOutKernel(TxKernel::Ptr& kernel, uint32_t assetIdx, ECC::Scalar::Native& offset)
    {
        Scalar::Native sk;

        auto kernelKeyId = Key::ID(assetIdx,  Key::Type::Kernel, assetIdx);
        m_MasterKdf->DeriveKey(sk, kernelKeyId);
        kernel->m_Commitment = ECC::Context::get().G * sk;
        kernel->Sign(sk);

        offset += -sk;
    }

    void LocalPrivateKeyKeeper::SignEmissionKernel(TxKernel::Ptr& kernel, uint32_t assetIdx, ECC::Scalar::Native& offset)
    {
        auto assetID = AIDFromKeyIndex(assetIdx);
        kernel->m_Commitment.m_X = assetID;
        kernel->m_Commitment.m_Y = 0;

        auto assetKey = GetAssetKey(Key::ID(assetIdx, Key::Type::Asset, assetIdx));
        kernel->Sign(assetKey);
        offset += -assetKey;
    }
}