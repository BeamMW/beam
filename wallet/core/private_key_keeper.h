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
    using WalletIDKey = uint64_t;

    //
    // Interface to master key storage. HW wallet etc.
    // Only public info should cross its boundary.
    //
    struct IPrivateKeyKeeper2
    {
        typedef std::shared_ptr<IPrivateKeyKeeper2> Ptr;

        struct Slot
        {
            typedef uint32_t Type;
            static const Type Invalid;
        };

        struct Status
        {
            typedef int Type;

            static const Type Success = 0;
            static const Type InProgress = -1;
            static const Type Unspecified = 1;
            static const Type UserAbort = 2;
            static const Type NotImplemented = 3;
        };

        enum struct KdfType {
            Root,
            Sbbs,
        };

        struct Handler
        {
            typedef std::shared_ptr<Handler> Ptr;

            virtual ~Handler() {}
            virtual void OnDone(Status::Type) = 0;
        };

        struct ShieldedInput
            : public ShieldedTxo::ID
        {
            Amount m_Fee;

            template <typename Archive>
			void serialize(Archive& ar)
			{
			    ShieldedTxo::ID::serialize(ar);
				ar & m_Fee;
			}
        };

        struct Method
        {
            struct get_Kdf
            {
                KdfType m_Type;

                Key::IKdf::Ptr m_pKdf; // only for trusted host
                Key::IPKdf::Ptr m_pPKdf;
            };

            struct get_NumSlots {
                Slot::Type m_Count;
            };

            struct get_Commitment {
                CoinID m_Cid;
                ECC::Point m_Result;
            };

            struct CreateOutput {
                Height m_hScheme; // scheme prior to Fork1 isn't supported for trustless wallet
                CoinID m_Cid; // weak schemes (V0, BB21) isn't supported for trustless wallet
                Output::User m_User;
                Output::Ptr m_pResult;

                CreateOutput() { ZeroObject(m_User); }
            };

            struct CreateInputShielded
                :public ShieldedTxo::ID
            {
                Sigma::CmList* m_pList;
                uint32_t m_iIdx;

                TxKernelShieldedInput::Ptr m_pKernel;
                // before invocation the following must be set:
                //  Fee, min/max Heights
                //  m_WindowEnd
                //  m_SpendProof.m_Cfg
                //  m_pKernel/m_hvShieldedState
            };

            struct CreateVoucherShielded
            {
                WalletIDKey m_MyIDKey = 0;
                ECC::Hash::Value m_Nonce;
                uint32_t m_Count = 1; // the result amount of vouchers may be less (i.e. there's an internal limit)
                std::vector<ShieldedTxo::Voucher> m_Res;
            };

            struct InOuts
            {
                std::vector<CoinID> m_vInputs;
                std::vector<CoinID> m_vOutputs;
                std::vector<ShieldedInput> m_vInputsShielded;
            };

            struct TxCommon :public InOuts
            {
                TxKernelStd::Ptr m_pKernel;
                ECC::Scalar::Native m_kOffset;

                bool m_NonConventional = false; // trusted mode only. Needed for synthetic txs, such as multisig, lock, swap and etc.
                // Balance doesn't have to match send/receive semantics, payment confirmation is neither generated nor verified
            };

            struct TxMutual :public TxCommon
            {
                // for mutually-constructed kernel
                PeerID m_Peer;
                WalletIDKey m_MyIDKey; // Must set for trustless wallet
                ECC::Signature m_PaymentProofSignature;
            };

            struct SignReceiver :public TxMutual {
            };

            struct SignSender :public TxMutual {
                Slot::Type m_Slot;
                ECC::Hash::Value m_UserAgreement; // set to Zero on 1st invocation
                PeerID m_MyID; // set in legacy mode (where it was sbbs pubkey) instead of m_MyIDKey. Otherwise it'll be set automatically.
            };

            struct SignSplit :public TxCommon {
                // send funds to yourself. in/out difference must be equal to fee
            };

            struct SignSendShielded :public TxCommon
            {
                ShieldedTxo::Voucher m_Voucher;
                PeerID m_Peer;
                WalletIDKey m_MyIDKey = 0; // set if sending to yourself (though makes no sense to do so)

                // sent value and asset are derived from the tx balance (ins - outs)
                ShieldedTxo::User m_User;
                bool m_HideAssetAlways = false;
            };

            struct DisplayWalletID
            {
                WalletIDKey m_MyIDKey = 0;
            };

        };

#define KEY_KEEPER_METHODS(macro) \
		macro(get_Kdf) \
		macro(get_NumSlots) \
		macro(get_Commitment) \
		macro(CreateOutput) \
		macro(CreateInputShielded) \
		macro(CreateVoucherShielded) \
		macro(SignReceiver) \
		macro(SignSender) \
		macro(SignSendShielded) \
		macro(SignSplit) \
		macro(DisplayWalletID) \


#define THE_MACRO(method) \
			virtual Status::Type InvokeSync(Method::method&); \
			virtual void InvokeAsync(Method::method&, const Handler::Ptr&) = 0;

        KEY_KEEPER_METHODS(THE_MACRO)
#undef THE_MACRO

        virtual ~IPrivateKeyKeeper2() {}

    private:
        struct HandlerSync;

        template <typename TMethod>
        Status::Type InvokeSyncInternal(TMethod& m);
    };

    // implements async notification mechanism, base for async implementations
    class PrivateKeyKeeper_WithMarshaller
        :public IPrivateKeyKeeper2
    {
    protected:

		io::AsyncEvent::Ptr m_pNewOut;

        struct Task
            :public boost::intrusive::list_base_hook<>
        {
            typedef std::unique_ptr<Task> Ptr;

            Handler::Ptr m_pHandler;

            virtual void Execute(Task::Ptr&) = 0;
            virtual ~Task() {} // necessary for derived classes, that may add arbitrary data memebers
        };

        struct TaskFin
            :public Task
        {
            Status::Type m_Status;

            virtual void Execute(Task::Ptr&) override;
            virtual ~TaskFin() {}
        };

		struct TaskList
			:public boost::intrusive::list<Task>
		{
            void Pop(Task::Ptr&);
            bool Push(Task::Ptr&); // returns if was empty
            void Clear();

			~TaskList() { Clear(); }
		};

        std::mutex m_MutexOut;
        TaskList m_queOut;

        void EnsureEvtOut();
        void PushOut(Task::Ptr& p);
        void PushOut(Status::Type, const Handler::Ptr&);

        void OnNewOut();
    };

	struct PrivateKeyKeeper_AsyncNotify // by default emulates async calls by synchronous, and then asynchronously posts completion status
		:public PrivateKeyKeeper_WithMarshaller
	{
#define THE_MACRO(method) \
		void InvokeAsync(Method::method& m, const Handler::Ptr& pHandler) override;

		KEY_KEEPER_METHODS(THE_MACRO)
#undef THE_MACRO

	};

	class ThreadedPrivateKeyKeeper
		:public PrivateKeyKeeper_WithMarshaller
	{
        IPrivateKeyKeeper2::Ptr m_pKeyKeeper;

		MyThread m_Thread;
		bool m_Run = true;

		std::mutex m_MutexIn;
		std::condition_variable m_NewIn;

        struct Task
            :public TaskFin
        {
            virtual void Exec(IPrivateKeyKeeper2&) = 0;
        };

		TaskList m_queIn;

        void PushIn(Task::Ptr& p);
        void Thread(const Rules&);

    public:

        ThreadedPrivateKeyKeeper(const IPrivateKeyKeeper2::Ptr& p);
        ~ThreadedPrivateKeyKeeper();

		template <typename TMethod>
        void InvokeAsyncInternal(TMethod& m, const Handler::Ptr& pHandler);

#define THE_MACRO(method) \
		void InvokeAsync(Method::method& m, const Handler::Ptr& pHandler) override;

		KEY_KEEPER_METHODS(THE_MACRO)
#undef THE_MACRO

	};

}
