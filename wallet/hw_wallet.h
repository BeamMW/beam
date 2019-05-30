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

namespace beam
{
    class HWWalletImpl;

    class HWWallet
    {
    public:
        HWWallet();

        template<typename T> using Result = std::function<void(const T& key)>;

        void getOwnerKey(Result<std::string> callback) const;
        void generateNonce(uint8_t slot, Result<ECC::Point> callback) const;
        void generateKey(const ECC::Key::IDV& idv, bool isCoinKey, Result<std::string> callback) const;

        std::string getOwnerKeySync() const;
        ECC::Point generateNonceSync(uint8_t slot) const;
        std::string generateKeySync(const ECC::Key::IDV& idv, bool isCoinKey) const;

    private:
        std::shared_ptr<HWWalletImpl> m_impl;
    };
}