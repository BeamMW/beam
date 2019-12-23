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
        boost::optional<ECC::Hash::Value> lockImage;
		boost::optional<ECC::Hash::Value> lockPreImage;
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
        template<typename R1, typename R2>
        using CallbackEx = std::function<void(R1&&, R2&&)>;
        using ExceptionCallback = Callback<const std::exception&>;
        using PublicKeys = std::vector<ECC::Point>;
        using RangeProofs = std::vector<std::unique_ptr<ECC::RangeProof::Confidential>>;
        using Outputs = std::vector<Output::Ptr>;

        struct Nonce
        {
            uint8_t m_Slot = 0;
            ECC::Point m_PublicValue;
        };

        virtual void GeneratePublicKeys(const std::vector<Key::IDV>& ids, bool createCoinKey, Callback<PublicKeys>&&, ExceptionCallback&&) = 0;
        virtual void GenerateOutputs(Height schemeHeigh, const std::vector<Key::IDV>& ids, Callback<Outputs>&&, ExceptionCallback&&) = 0;
        virtual void GenerateOutputsEx(Height schemeHeigh, const std::vector<Key::IDV>& ids, const AssetID& assetId, CallbackEx<Outputs, ECC::Scalar::Native>&&, ExceptionCallback&&) = 0;

        virtual size_t AllocateNonceSlot() = 0;

        // sync part for integration test
        virtual PublicKeys GeneratePublicKeysSync(const std::vector<Key::IDV>& ids, bool createCoinKey) = 0;
        virtual std::pair<PublicKeys, ECC::Scalar::Native> GeneratePublicKeysSyncEx(const std::vector<Key::IDV>& ids, bool createCoinKey, const AssetID& assetID) = 0;

        virtual ECC::Point GeneratePublicKeySync(const Key::IDV& id) = 0;
        virtual ECC::Point GenerateCoinKeySync(const Key::IDV& id, const AssetID& assetId) = 0;
        virtual Outputs GenerateOutputsSync(Height schemeHeigh, const std::vector<Key::IDV>& ids) = 0;
        virtual std::pair<Outputs, ECC::Scalar::Native> GenerateOutputsSyncEx(Height schemeHeigh, const std::vector<Key::IDV>& ids, const AssetID& assetId) = 0;

        virtual ECC::Point GenerateNonceSync(size_t slot) = 0;
        virtual ECC::Scalar SignSync(const std::vector<Key::IDV>& inputs, const std::vector<Key::IDV>& outputs, const AssetID& assetId, const ECC::Scalar::Native& offset, size_t nonceSlot, const KernelParameters& kernelParamerters, const ECC::Point::Native& publicNonce) = 0;
        virtual Key::IKdf::Ptr get_SbbsKdf() const = 0;
        virtual void subscribe(Handler::Ptr handler) = 0;

        //
        // For assets
        //
        virtual ECC::Scalar::Native SignEmissionKernel(TxKernelAssetEmit& kernel, uint32_t assetIdx) = 0;
        virtual AssetID GetAssetID(uint32_t assetIdx) = 0;
    };
}