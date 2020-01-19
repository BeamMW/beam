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

#pragma once

#include "wallet/core/private_key_keeper.h"
#include "wallet/core/variables_db.h"
#include <utility>

namespace beam::wallet
{
    //
    // Private key keeper in local storage implementation
    //
    class LocalPrivateKeyKeeper : public IPrivateKeyKeeper
        , public std::enable_shared_from_this<LocalPrivateKeyKeeper>
    {
    public:
        LocalPrivateKeyKeeper(IVariablesDB::Ptr variablesDB, Key::IKdf::Ptr kdf);
        virtual ~LocalPrivateKeyKeeper();
    private:
        void GeneratePublicKeys(const std::vector<Key::IDV>& ids, bool createCoinKey, Callback<PublicKeys>&& resultCallback, ExceptionCallback&& exceptionCallback) override;
        void GeneratePublicKeysEx(const std::vector<Key::IDV>& ids, bool createCoinKey, Asset::ID assetID, Callback<PublicKeys>&&, ExceptionCallback&&) override;
        void GenerateOutputs(Height schemeHeight, const std::vector<Key::IDV>& ids, Callback<Outputs>&&, ExceptionCallback&&) override;
        void GenerateOutputsEx(Height schemeHeight, const std::vector<Key::IDV>& ids, Asset::ID assetId, Callback<Outputs>&&, ExceptionCallback&&) override;

        void SignReceiver(const std::vector<Key::IDV>& inputs
                        , const std::vector<Key::IDV>& outputs
                        , Asset::ID assetId
                        , const KernelParameters& kernelParamerters
                        , const WalletIDKey& walletIDkey
                        , Callback<ReceiverSignature>&&, ExceptionCallback&&) override;
        void SignSender(const std::vector<Key::IDV>& inputs
                      , const std::vector<Key::IDV>& outputs
                      , Asset::ID assetId
                      , size_t nonceSlot
                      , const KernelParameters& kernelParamerters
                      , bool initial
                      , Callback<SenderSignature>&&, ExceptionCallback&&) override;


        size_t AllocateNonceSlotSync() override;

        PublicKeys GeneratePublicKeysSync(const std::vector<Key::IDV>& ids, bool createCoinKey) override;
        PublicKeys GeneratePublicKeysSyncEx(const std::vector<Key::IDV>& ids, bool createCoinKey, Asset::ID) override;

        ECC::Point GeneratePublicKeySync(const Key::IDV& id) override;
        ECC::Point GenerateCoinKeySync(const Key::IDV& id, Asset::ID) override;
        Outputs GenerateOutputsSync(Height schemeHeigh, const std::vector<Key::IDV>& ids) override;
        Outputs GenerateOutputsSyncEx(Height schemeHeigh, const std::vector<Key::IDV>& ids, Asset::ID) override;

        //RangeProofs GenerateRangeProofSync(Height schemeHeight, const std::vector<Key::IDV>& ids) override;
        ECC::Point GenerateNonceSync(size_t slot) override;
        ECC::Scalar SignSync(const std::vector<Key::IDV>& inputs, const std::vector<Key::IDV>& outputs, Asset::ID, const ECC::Scalar::Native& offset, size_t nonceSlot, const KernelParameters& kernelParameters, const ECC::Point::Native& publicNonce) override;

        ReceiverSignature SignReceiverSync(const std::vector<Key::IDV>& inputs
                                     , const std::vector<Key::IDV>& outputs
                                     , Asset::ID
                                     , const KernelParameters& kernelParamerters
                                     , const WalletIDKey& walletIDkey) override;
        SenderSignature SignSenderSync(const std::vector<Key::IDV>& inputs
                                 , const std::vector<Key::IDV>& outputs
                                 , Asset::ID
                                 , size_t nonceSlot
                                 , const KernelParameters& kernelParamerters
                                 , bool initial) override;

        Key::IKdf::Ptr get_SbbsKdf() const override;

        void subscribe(Handler::Ptr handler) override {}

        //
        // Assets
        //
        PeerID GetAssetOwnerID(Key::Index assetOwnerIdx) override;
        ECC::Scalar::Native SignEmissionKernel(TxKernelAssetEmit& kernel, Key::Index assetOwnerIdx) override;

    private:
        // pair<asset public (asset id), asset private>
        std::pair<PeerID, ECC::Scalar::Native> GetAssetOwnerKeypair(Key::Index assetOwnerIdx);

        Key::IKdf::Ptr GetChildKdf(const Key::IDV&) const;
        ECC::Scalar::Native GetNonce(size_t slot);
        ECC::Scalar::Native GetExcess(const std::vector<Key::IDV>& inputs, const std::vector<Key::IDV>& outputs, Asset::ID, const ECC::Scalar::Native& offset) const;
        int64_t CalculateValue(const std::vector<Key::IDV>& inputs, const std::vector<Key::IDV>& outputs) const;
        void LoadNonceSeeds();
        void SaveNonceSeeds();

        struct KeyPair
        {
            ECC::Scalar::Native m_PrivateKey;
            PeerID m_PublicKey;
        };
        KeyPair GetWalletID(const WalletIDKey& walletKeyID) const;


        template <typename Func, typename ...Args>
        auto MakeAsyncFunc(Func&& func, Args... args)
        {
            return [this, func, args...]() mutable
            {
                return (this->*func)(std::forward<Args>(args)...);
            };
        }

        template <typename Result, typename Func>
        void DoAsync(Func&& asyncFunc, Callback<Result>&& resultCallback, ExceptionCallback&& exceptionCallback)
        {
            try
            {
                resultCallback(asyncFunc());
            }
            catch (...)
            {
                exceptionCallback(std::current_exception());
            }
        }

        template <typename Result, typename Func>
        void DoThreadAsync(Func&& asyncFunc, Callback<Result>&& resultCallback, ExceptionCallback&& exceptionCallback)
        {
            using namespace std;
            auto thisHolder = shared_from_this();
            shared_ptr<Result> result = make_shared<Result>();
            shared_ptr<exception_ptr> storedException = make_shared<exception_ptr>();
            shared_ptr<future<void>> futureHolder = std::make_shared<future<void>>();
            *futureHolder = do_thread_async(
                [thisHolder, asyncFunc, result, storedException]()
                {
                    try
                    {
                        *result = asyncFunc();
                    }
                    catch (...)
                    {
                        *storedException = current_exception();
                    }
                },
                [futureHolder, resultCallback = move(resultCallback), exceptionCallback = move(exceptionCallback), result, storedException]() mutable
                {
                    if (*storedException)
                    {
                        exceptionCallback(move(*storedException));
                    }
                    else
                    {
                        resultCallback(move(*result));
                    }
                    futureHolder.reset();
                });
        }

    private:
        IVariablesDB::Ptr m_Variables;
        Key::IKdf::Ptr m_MasterKdf;

        struct MyNonce :public ECC::NoLeak<ECC::Hash::Value> {
            template <typename Archive> void serialize(Archive& ar) {
                ar& V;
            }
        };

        std::vector<MyNonce> m_Nonces;
        size_t m_NonceSlotLast = 0;
    };
}