// Copyright 2018 The Beam Team
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

#include "core/common.h"
#include "core/ecc.h"
#include "core/ecc_native.h"
#include "core/block_crypt.h"

class Client;
class DeviceManager;

namespace beam
{
    class HWWallet
    {
    public:
        using OnError = std::function<void(const std::string&)>;

        HWWallet(OnError onError = OnError());

        using Ptr = std::shared_ptr<beam::HWWallet>;

        std::vector<std::string> getDevices() const;
        bool isConnected() const;

        template<typename T> using Result = std::function<void(const T& key)>;

        struct TxData
        {
            beam::HeightRange height;
            beam::Amount fee;
            ECC::Point kernelCommitment;
            ECC::Point kernelNonce;
            uint32_t nonceSlot;
            ECC::Scalar offset;
        };

        void getOwnerKey(Result<std::string> callback) const;
        void generateNonce(uint8_t slot, Result<ECC::Point> callback) const;
        void getNoncePublic(uint8_t slot, Result<ECC::Point> callback) const;
        void generateKey(const ECC::Key::IDV& idv, bool isCoinKey, Result<ECC::Point> callback) const;
        void generateRangeProof(const ECC::Key::IDV& idv, bool isCoinKey, Result<ECC::RangeProof::Confidential> callback) const;
        void signTransaction(const std::vector<Key::IDV>& inputs, const std::vector<Key::IDV>& outputs, const TxData& tx, Result<ECC::Scalar> callback) const;

        std::string getOwnerKeySync() const;
        ECC::Point generateNonceSync(uint8_t slot) const;
        ECC::Point getNoncePublicSync(uint8_t slot) const;
        ECC::Point generateKeySync(const ECC::Key::IDV& idv, bool isCoinKey) const;
        ECC::RangeProof::Confidential generateRangeProofSync(const ECC::Key::IDV& idv, bool isCoinKey) const;
        ECC::Scalar signTransactionSync(const std::vector<Key::IDV>& inputs, const std::vector<Key::IDV>& outputs, const TxData& tx) const;

    private:
        std::shared_ptr<Client> m_client;
    };
}