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
                    Callback<ECC::Scalar::Native>&&,
                    ExceptionCallback&&) = 0;

        virtual ECC::Scalar::Native SignAssetKernelSync(const std::vector<CoinID>& inputs,
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
        };

        struct Handler
        {
            typedef std::shared_ptr<Handler> Ptr;

            virtual ~Handler() {}
            virtual void OnDone(Status::Type) = 0;
        };

        struct Method
        {
            struct get_KeyImage {
                ECC::uintBig m_Hash;
                uint32_t m_iGen; // G, J, H
                ECC::Point::Native m_Result;
            };

            struct get_NumSlots {
                uint32_t m_Count;
            };

            struct get_SlotImage {
                uint32_t m_iSlot;
                uint32_t m_iGen; // G, J, H
                ECC::Point::Native m_Result;
            };
        };

#define KEY_KEEPER_METHODS(macro) \
		macro(get_KeyImage) \
		macro(get_NumSlots) \
		macro(get_SlotImage) \


#define THE_MACRO(method) \
			virtual Status::Type InvokeSync(Method::method&); \
			virtual void InvokeAsync(Method::method&, const Handler::Ptr&);

        KEY_KEEPER_METHODS(THE_MACRO)
#undef THE_MACRO

        virtual ~IPrivateKeyKeeper2() {}

    private:
        struct HandlerSync;

        template <typename TMethod>
        Status::Type InvokeSyncInternal(TMethod& m);
    };
}