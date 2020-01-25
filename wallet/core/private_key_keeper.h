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

#include "common.h"
#include <boost/intrusive/list.hpp>

namespace beam::wallet
{
    struct KernelParameters
    {
        beam::HeightRange height;
        beam::Amount fee;
        ECC::Point commitment;
        ECC::Point publicNonce;
        boost::optional<ECC::Hash::Value> lockImage;
        boost::optional<ECC::Hash::Value> lockPreImage;
        ECC::Signature paymentProofSignature;
        PeerID peerID = Zero;
        PeerID myID;

        SERIALIZE(height.m_Min, height.m_Max, fee, commitment, publicNonce, lockImage, lockPreImage, paymentProofSignature, peerID, myID)
    };

    struct ReceiverSignature
    {
        ECC::Signature m_KernelSignature;
        ECC::Signature m_PaymentProofSignature;
        ECC::Scalar m_Offset;
        ECC::Point m_KernelCommitment;
    };

    struct SenderSignature
    {
        ECC::Signature m_KernelSignature;
        ECC::Scalar m_Offset;
        ECC::Point m_KernelCommitment;
    };

    struct AssetSignature
    {
        PeerID m_AssetOwnerId = 0UL;
        ECC::Scalar m_Offset;
    };

    using WalletIDKey = uint64_t;

    class KeyKeeperException : public std::runtime_error
    {
    public:
        explicit KeyKeeperException(const std::string& message) : std::runtime_error(message) {}
        explicit KeyKeeperException(const char* message) : std::runtime_error(message) {}
    };

    class InvalidPaymentProofException : public KeyKeeperException
    {
    public:
        InvalidPaymentProofException() : KeyKeeperException("Invalid payment proof") {}
    };

    class InvalidParametersException : public KeyKeeperException
    {
    public:
        InvalidParametersException() : KeyKeeperException("Invalid signature parameters") {}
    };


    //
    // Interface to master key storage. HW wallet etc.
    // Only public info should cross its boundary.
    //
    struct IPrivateKeyKeeper
    {
        struct Handler
        {
            using Ptr = Handler*;

            virtual void onShowKeyKeeperMessage() = 0;
            virtual void onHideKeyKeeperMessage() = 0;
            virtual void onShowKeyKeeperError(const std::string&) = 0;
        };

        using Ptr = std::shared_ptr<IPrivateKeyKeeper>;

        template<typename R>
        using Callback = std::function<void(R&&)>;

        using ExceptionCallback = Callback<std::exception_ptr>;
        
        using PublicKeys = std::vector<ECC::Point>;
        using RangeProofs = std::vector<std::unique_ptr<ECC::RangeProof::Confidential>>;
        using Outputs = std::vector<Output::Ptr>;

        virtual void GeneratePublicKeys(const std::vector<CoinID>& ids, bool createCoinKey, Callback<PublicKeys>&&, ExceptionCallback&&) = 0;
        virtual void GenerateOutputs(Height schemeHeigh, const std::vector<CoinID>& ids, Callback<Outputs>&&, ExceptionCallback&&) = 0;

        virtual void SignReceiver(const std::vector<CoinID>& inputs
                                , const std::vector<CoinID>& outputs
                                , const KernelParameters& kernelParamerters
                                , const WalletIDKey& walletIDkey
                                , Callback<ReceiverSignature>&&, ExceptionCallback&&) = 0;
        virtual void SignSender(const std::vector<CoinID>& inputs
                              , const std::vector<CoinID>& outputs
                              , size_t nonceSlot
                              , const KernelParameters& kernelParamerters
                              , bool initial
                              , Callback<SenderSignature>&&, ExceptionCallback&&) = 0;


        // sync part for integration test
        virtual size_t AllocateNonceSlotSync() = 0;
        virtual PublicKeys GeneratePublicKeysSync(const std::vector<CoinID>& ids, bool createCoinKey) = 0;

        virtual ECC::Point GeneratePublicKeySync(const ECC::uintBig& id) = 0;
        virtual ECC::Point GenerateCoinKeySync(const CoinID&) = 0;
        virtual Outputs GenerateOutputsSync(Height schemeHeigh, const std::vector<CoinID>& ids) = 0;

        virtual ECC::Point GenerateNonceSync(size_t slot) = 0;

        virtual ReceiverSignature SignReceiverSync(const std::vector<CoinID>& inputs
                                             , const std::vector<CoinID>& outputs
                                             , const KernelParameters& kernelParamerters
                                             , const WalletIDKey& walletIDkey) = 0;
        virtual SenderSignature SignSenderSync(const std::vector<CoinID>& inputs
                                         , const std::vector<CoinID>& outputs
                                         , size_t nonceSlot
                                         , const KernelParameters& kernelParamerters
                                         , bool initial) = 0;

        virtual Key::IKdf::Ptr get_SbbsKdf() const = 0;
        virtual void subscribe(Handler::Ptr handler) = 0;

        //
        // For assets
        //
        virtual void SignAssetKernel(const std::vector<CoinID>& inputs,
                    const std::vector<CoinID>& outputs,
                    Amount fee,
                    Key::Index assetOwnerIdx,
                    TxKernelAssetControl& kernel,
                    Callback<AssetSignature>&&,
                    ExceptionCallback&&) = 0;

        virtual AssetSignature SignAssetKernelSync(const std::vector<CoinID>& inputs,
                    const std::vector<CoinID>& outputs,
                    Amount fee,
                    Key::Index assetOwnerIdx,
                    TxKernelAssetControl& kernel
                ) = 0;

        virtual PeerID GetAssetOwnerID(Key::Index assetOwnerIdx) = 0;
    };

    struct IPrivateKeyKeeper2
    {
        typedef std::shared_ptr<IPrivateKeyKeeper2> Ptr;

        struct Status
        {
            typedef int Type;

            static const Type Success = 0;
            static const Type InProgress = -1;
            static const Type Unspecified = 1;
            static const Type UserAbort = 2;
            static const Type NotImplemented = 3;
        };

        struct Handler
        {
            typedef std::shared_ptr<Handler> Ptr;

            virtual ~Handler() {}
            virtual void OnDone(Status::Type) = 0;
        };

        struct Method
        {
            struct get_Kdf
            {
                Key::Index m_iChild;
                bool m_Root; // if true the m_iChild is ignored

                Key::IKdf::Ptr m_pKdf; // only for trusted host
                Key::IPKdf::Ptr m_pPKdf;

                void From(const CoinID&);
            };

            struct get_NumSlots {
                uint32_t m_Count;
            };

            struct CreateOutput {
                Height m_hScheme; // scheme prior to Fork1 isn't supported for trustless wallet
                CoinID m_Cid; // weak schemes (V0, BB21) isn't supported for trustless wallet
                Output::Ptr m_pResult;
            };

            struct KernelCommon
            {
                beam::HeightRange m_Height;
                beam::Amount m_Fee;
                ECC::Point m_Commitment;
                ECC::Signature m_Signature;

                void To(TxKernelStd&) const;
                void From(const TxKernelStd&);
            };

            struct InOuts
            {
                std::vector<CoinID> m_vInputs;
                std::vector<CoinID> m_vOutputs;
            };

            struct TxCommon :public InOuts
            {
                KernelCommon m_KernelParams;
                ECC::Scalar::Native m_kOffset;
            };

            struct TxMutual :public TxCommon
            {
                // for mutually-constructed kernel
                PeerID m_Peer;
                WalletIDKey m_MyID;
                ECC::Signature m_PaymentProofSignature;
            };

            struct SignReceiver :public TxMutual {
            };

            struct SignSender :public TxMutual {
                uint32_t m_nonceSlot;
                ECC::Hash::Value m_UserAgreement; // set to Zero on 1st invocation
            };
        };

#define KEY_KEEPER_METHODS(macro) \
		macro(get_Kdf) \
		macro(get_NumSlots) \
		macro(CreateOutput) \
		macro(SignReceiver) \
		macro(SignSender) \


#define THE_MACRO(method) \
			virtual Status::Type InvokeSync(Method::method&); \
			virtual void InvokeAsync(Method::method&, const Handler::Ptr&) = 0;

        KEY_KEEPER_METHODS(THE_MACRO)
#undef THE_MACRO

        virtual ~IPrivateKeyKeeper2() {}

        // synthetic functions (in terms of underlying ones)
        Status::Type get_Commitment(ECC::Point::Native&, const CoinID&);

    private:
        struct HandlerSync;

        template <typename TMethod>
        Status::Type InvokeSyncInternal(TMethod& m);
    };


	class PrivateKeyKeeper_AsyncNotify // by default emulates async calls by synchronous, and then asynchronously posts completion status
		:public IPrivateKeyKeeper2
	{
    protected:

		io::AsyncEvent::Ptr m_pNewOut;

        struct Task
            :public boost::intrusive::list_base_hook<>
        {
            typedef std::unique_ptr<Task> Ptr;

            Handler::Ptr m_pHandler;
            Status::Type m_Status;

            virtual ~Task() {} // necessary for derived classes, that may add arbitrary data memebers
        };

		struct TaskList
			:public boost::intrusive::list<Task>
		{
            void Pop(Task::Ptr&);
            bool Push(Task::Ptr&); // returns if was empty
            void Clear();

			~TaskList() { Clear(); }
		};

		TaskList m_queIn;
		TaskList m_queOut;

        void EnsureEvtOut();
        void PushOut(Task::Ptr& p);
        void PushOut(Status::Type, const Handler::Ptr&);
        
        virtual void OnNewOut();
        static void CallNewOut(TaskList&);

    public:

#define THE_MACRO(method) \
		virtual void InvokeAsync(Method::method& m, const Handler::Ptr& pHandler) override;

		KEY_KEEPER_METHODS(THE_MACRO)
#undef THE_MACRO

	};

	class ThreadedPrivateKeyKeeper
		:public PrivateKeyKeeper_AsyncNotify
	{
        IPrivateKeyKeeper2::Ptr m_pKeyKeeper;

		std::thread m_Thread;
		bool m_Run = true;

		std::mutex m_MutexIn;
		std::condition_variable m_NewIn;

		std::mutex m_MutexOut;

        struct Task
            :public PrivateKeyKeeper_AsyncNotify::Task
        {
            virtual void Exec(IPrivateKeyKeeper2&) = 0;
        };

		TaskList m_queIn;

        void PushIn(Task::Ptr& p);
        void Thread();

        virtual void OnNewOut() override;

    public:

        ThreadedPrivateKeyKeeper(const IPrivateKeyKeeper2::Ptr& p);
        ~ThreadedPrivateKeyKeeper();

		template <typename TMethod>
        void InvokeAsyncInternal(TMethod& m, const Handler::Ptr& pHandler);

#define THE_MACRO(method) \
		virtual void InvokeAsync(Method::method& m, const Handler::Ptr& pHandler) override;

		KEY_KEEPER_METHODS(THE_MACRO)
#undef THE_MACRO

	};

}
