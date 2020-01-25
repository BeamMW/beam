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
        void GeneratePublicKeys(const std::vector<CoinID>& ids, bool createCoinKey, Callback<PublicKeys>&& resultCallback, ExceptionCallback&& exceptionCallback) override;
        void GenerateOutputs(Height schemeHeight, const std::vector<CoinID>& ids, Callback<Outputs>&&, ExceptionCallback&&) override;

        void SignReceiver(const std::vector<CoinID>& inputs
                        , const std::vector<CoinID>& outputs
                        , const KernelParameters& kernelParamerters
                        , const WalletIDKey& walletIDkey
                        , Callback<ReceiverSignature>&&, ExceptionCallback&&) override;
        void SignSender(const std::vector<CoinID>& inputs
                      , const std::vector<CoinID>& outputs
                      , size_t nonceSlot
                      , const KernelParameters& kernelParamerters
                      , bool initial
                      , Callback<SenderSignature>&&, ExceptionCallback&&) override;


        size_t AllocateNonceSlotSync() override;

        PublicKeys GeneratePublicKeysSync(const std::vector<CoinID>& ids, bool createCoinKey) override;

        ECC::Point GeneratePublicKeySync(const ECC::uintBig&) override;
        ECC::Point GenerateCoinKeySync(const CoinID& id) override;
        Outputs GenerateOutputsSync(Height schemeHeigh, const std::vector<CoinID>& ids) override;

        ECC::Point GenerateNonceSync(size_t slot) override;

        ReceiverSignature SignReceiverSync(const std::vector<CoinID>& inputs
                                     , const std::vector<CoinID>& outputs
                                     , const KernelParameters& kernelParamerters
                                     , const WalletIDKey& walletIDkey) override;
        SenderSignature SignSenderSync(const std::vector<CoinID>& inputs
                                 , const std::vector<CoinID>& outputs
                                 , size_t nonceSlot
                                 , const KernelParameters& kernelParamerters
                                 , bool initial) override;

        Key::IKdf::Ptr get_SbbsKdf() const override;

        void subscribe(Handler::Ptr handler) override {}

        //
        // Assets
        //
        PeerID GetAssetOwnerID(Key::Index assetOwnerIdx) override;

        void SignAssetKernel(const std::vector<CoinID>& inputs,
                const std::vector<CoinID>& outputs,
                Amount fee,
                Key::Index assetOwnerIdx,
                TxKernelAssetControl& kernel,
                Callback<AssetSignature>&&,
                ExceptionCallback&&) override;

        AssetSignature SignAssetKernelSync(const std::vector<CoinID>& inputs,
                const std::vector<CoinID>& outputs,
                Amount fee,
                Key::Index assetOwnerIdx,
                TxKernelAssetControl& kernel) override;

    private:
        // pair<asset public (asset id), asset private>
        std::pair<PeerID, ECC::Scalar::Native> GetAssetOwnerKeypair(Key::Index assetOwnerIdx);

        Key::IKdf::Ptr GetChildKdf(const CoinID&) const;
        ECC::Scalar::Native GetNonce(size_t slot);
        ECC::Scalar::Native GetExcess(const std::vector<CoinID>& inputs, const std::vector<CoinID>& outputs, const ECC::Scalar::Native& offset) const;
        int64_t CalculateValue(const std::vector<CoinID>& inputs, const std::vector<CoinID>& outputs) const;
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


    class LocalPrivateKeyKeeper2
        : public PrivateKeyKeeper_AsyncNotify
    {
        static Status::Type ToImage(ECC::Point::Native& res, uint32_t iGen, const ECC::Scalar::Native& sk);

        struct Aggregation
        {
            ECC::Scalar::Native m_sk;
            AmountBig::Type m_ValBig;
            AmountBig::Type m_ValBigAsset;
            Asset::ID m_AssetID;

            Amount m_Val;
            Amount m_ValAsset;
        };

        bool Aggregate(Aggregation&, const Method::TxCommon&, bool bSending);
        bool Aggregate(Aggregation&, const std::vector<CoinID>&, bool bOuts);

        static void UpdateOffset(Method::TxCommon&, const ECC::Scalar::Native& kDiff, const ECC::Scalar::Native& kKrn);

    public:

        LocalPrivateKeyKeeper2(const ECC::Key::IKdf::Ptr&);

#define THE_MACRO(method) \
        virtual Status::Type InvokeSync(Method::method& m) override;

        KEY_KEEPER_METHODS(THE_MACRO)
#undef THE_MACRO

    protected:

        ECC::Key::IKdf::Ptr m_pKdf;

        // make nonce generation abstract, to enable testing the code with predefined nonces
        virtual uint32_t get_NumSlots() = 0;
        virtual void get_Nonce(ECC::Scalar::Native&, uint32_t) = 0;
        virtual void Regenerate(uint32_t) = 0;

        // user interaction emulation
        virtual bool IsTrustless() { return false; }
        virtual Status::Type ConfirmSpend(Amount, Asset::ID, const PeerID&, const TxKernel&, bool bFinal) { return Status::Success; }

    };

    class LocalPrivateKeyKeeperStd
        : public LocalPrivateKeyKeeper2
    {
    public:

        static const uint32_t s_Slots = 64;

        struct State
        {
            ECC::Hash::Value m_pSlot[s_Slots];
            ECC::Hash::Value m_hvLast;

            void Generate(); // must set m_hvLast before calling
            void Regenerate(uint32_t iSlot);

        } m_State;

        using LocalPrivateKeyKeeper2::LocalPrivateKeyKeeper2;

    protected:

        virtual uint32_t get_NumSlots() override;
        virtual void get_Nonce(ECC::Scalar::Native&, uint32_t) override;
        virtual void Regenerate(uint32_t) override;

    };
}
