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
        using PublicKeysEx = std::pair<PublicKeys, ECC::Scalar::Native>;
        using RangeProofs = std::vector<std::unique_ptr<ECC::RangeProof::Confidential>>;
        using Outputs = std::vector<Output::Ptr>;
        using OutputsEx = std::pair<Outputs, ECC::Scalar::Native>;

        virtual void GeneratePublicKeys(const std::vector<Key::IDV>& ids, bool createCoinKey, Callback<PublicKeys>&&, ExceptionCallback&&) = 0;
        virtual void GeneratePublicKeysEx(const std::vector<Key::IDV>& ids, bool createCoinKey, AssetID assetID, Callback<PublicKeysEx>&&, ExceptionCallback&&) = 0;
        virtual void GenerateOutputs(Height schemeHeigh, const std::vector<Key::IDV>& ids, Callback<Outputs>&&, ExceptionCallback&&) = 0;
        virtual void GenerateOutputsEx(Height schemeHeigh, const std::vector<Key::IDV>& ids, AssetID assetId, Callback<OutputsEx>&&, ExceptionCallback&&) = 0;

        virtual void SignReceiver(const std::vector<Key::IDV>& inputs
                                , const std::vector<Key::IDV>& outputs
                                , AssetID assetId
                                , const KernelParameters& kernelParamerters
                                , const WalletIDKey& walletIDkey
                                , Callback<ReceiverSignature>&&, ExceptionCallback&&) = 0;
        virtual void SignSender(const std::vector<Key::IDV>& inputs
                              , const std::vector<Key::IDV>& outputs
                              , AssetID assetId
                              , size_t nonceSlot
                              , const KernelParameters& kernelParamerters
                              , bool initial
                              , Callback<SenderSignature>&&, ExceptionCallback&&) = 0;


        // sync part for integration test
        virtual size_t AllocateNonceSlotSync() = 0;
        virtual PublicKeys GeneratePublicKeysSync(const std::vector<Key::IDV>& ids, bool createCoinKey) = 0;
        virtual PublicKeysEx GeneratePublicKeysSyncEx(const std::vector<Key::IDV>& ids, bool createCoinKey, AssetID assetID) = 0;

        virtual ECC::Point GeneratePublicKeySync(const Key::IDV& id) = 0;
        virtual ECC::Point GenerateCoinKeySync(const Key::IDV& id, AssetID) = 0;
        virtual Outputs GenerateOutputsSync(Height schemeHeigh, const std::vector<Key::IDV>& ids) = 0;
        virtual OutputsEx GenerateOutputsSyncEx(Height schemeHeigh, const std::vector<Key::IDV>& ids, AssetID assetId) = 0;

        virtual ECC::Point GenerateNonceSync(size_t slot) = 0;
        virtual ECC::Scalar SignSync(const std::vector<Key::IDV>& inputs, const std::vector<Key::IDV>& outputs, AssetID assetId, const ECC::Scalar::Native& offset, size_t nonceSlot, const KernelParameters& kernelParamerters, const ECC::Point::Native& publicNonce) = 0;

        virtual ReceiverSignature SignReceiverSync(const std::vector<Key::IDV>& inputs
                                             , const std::vector<Key::IDV>& outputs
                                             , AssetID assetId
                                             , const KernelParameters& kernelParamerters
                                             , const WalletIDKey& walletIDkey) = 0;
        virtual SenderSignature SignSenderSync(const std::vector<Key::IDV>& inputs
                                         , const std::vector<Key::IDV>& outputs
                                         , AssetID assetId
                                         , size_t nonceSlot
                                         , const KernelParameters& kernelParamerters
                                         , bool initial) = 0;

        virtual Key::IKdf::Ptr get_SbbsKdf() const = 0;
        virtual void subscribe(Handler::Ptr handler) = 0;

        //
        // For assets
        //
        virtual ECC::Scalar::Native SignAssetKernel(TxKernelAssetControl& kernel, Key::Index assetOwnerIdx) = 0;
        virtual PeerID GetAssetOwnerID(Key::Index assetOwnerIdx) = 0;
    };
}