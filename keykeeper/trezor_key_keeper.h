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
#include "hw_wallet.h"

namespace beam::wallet
{
    class TrezorKeyKeeper : public IPrivateKeyKeeper
        , public std::enable_shared_from_this<TrezorKeyKeeper>
    {
    public:
        TrezorKeyKeeper();
        virtual ~TrezorKeyKeeper();

        struct DeviceNotConnected : std::runtime_error 
        {
            DeviceNotConnected() : std::runtime_error("") {}
        };

        Key::IKdf::Ptr get_SbbsKdf() const override;
        void subscribe(Handler::Ptr handler) override;

    private:
        void GeneratePublicKeys(const std::vector<Key::IDV>& ids, bool createCoinKey, Callback<PublicKeys>&& resultCallback, ExceptionCallback&& exceptionCallback) override;
        void GenerateOutputs(Height schemeHeight, const std::vector<Key::IDV>& ids, Callback<Outputs>&& resultCallback, ExceptionCallback&& exceptionCallback) override;
        void GenerateOutputsEx(Height schemeHeight, const std::vector<Key::IDV>& ids, const AssetID& assetId, CallbackEx<Outputs, ECC::Scalar::Native>&&, ExceptionCallback&&) override;

        size_t AllocateNonceSlot() override;
        PublicKeys GeneratePublicKeysSync(const std::vector<Key::IDV>& ids, bool createCoinKey) override;
        std::pair<PublicKeys, ECC::Scalar::Native> GeneratePublicKeysSyncEx(const std::vector<Key::IDV>& ids, bool createCoinKey, const AssetID& assetID) override;

        ECC::Point GeneratePublicKeySync(const Key::IDV& id) override;
        ECC::Point GenerateCoinKeySync(const Key::IDV& id, const AssetID& assetId) override;
        Outputs GenerateOutputsSync(Height schemeHeigh, const std::vector<Key::IDV>& ids) override;
        std::pair<Outputs, ECC::Scalar::Native> GenerateOutputsSyncEx(Height schemeHeigh, const std::vector<Key::IDV>& ids, const AssetID& assetId) override;

        ECC::Point GenerateNonceSync(size_t slot) override;
        ECC::Scalar SignSync(const std::vector<Key::IDV>& inputs, const std::vector<Key::IDV>& outputs, const AssetID& assetId, const ECC::Scalar::Native& offset, size_t nonceSlot, const KernelParameters& kernelParamerters, const ECC::Point::Native& publicNonce) override;

        ECC::Scalar::Native SignEmissionKernel(TxKernelAssetEmit& kernel, uint32_t assetIdx) override;
        AssetID GetAssetID(uint32_t assetIdx) override;

    private:
        beam::HWWallet m_hwWallet;
        mutable Key::IKdf::Ptr m_sbbsKdf;

        size_t m_latestSlot;

        std::vector<IPrivateKeyKeeper::Handler::Ptr> m_handlers;
    };
}