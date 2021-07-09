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

#include "wallet/api/v6_0/v6_api.h"
#include "v6_1_api_defs.h"

namespace beam::wallet
{
    class V61Api
        : public V6Api
        , public IWalletObserver
    {
    public:
        // CTOR MUST BE SAFE TO CALL FROM ANY THREAD
        V61Api(IWalletApiHandler& handler, unsigned long avMajor, unsigned long avMinor, const ApiInitData& init);
        ~V61Api() override;

        V6_1_API_METHODS(BEAM_API_PARSE_FUNC)
        V6_1_API_METHODS(BEAM_API_RESPONSE_FUNC)
        V6_1_API_METHODS(BEAM_API_HANDLE_FUNC)

    protected:
        //
        // IWalletObserver
        //
        void onSyncProgress(int done, int total) override;
        void onSystemStateChanged(const Block::SystemState::ID& stateID) override;
        void onAssetsChanged(ChangeAction action, const std::vector<Asset::ID>&);
        void onAssetChanged(ChangeAction action, beam::Asset::ID) override;
        void onCoinsChanged(ChangeAction action, const std::vector<Coin>& items) override;
        void onShieldedCoinsChanged(ChangeAction action, const std::vector<ShieldedCoin>& items) override;
        void onAddressChanged(ChangeAction action, const std::vector<WalletAddress>& items) override;
        void onTransactionChanged(ChangeAction action, const std::vector<TxDescription>& items);

        template<typename T>
        void onCoinsChangedImp(ChangeAction action, const std::vector<T>& items);

        //
        // V6 behavior changes
        //
        void fillAssetInfo(json& parent, const WalletAsset& info) override;

    private:
        struct SubFlags {
            typedef uint32_t Type;
            static const uint32_t SyncProgress = 1 << 0;
            static const uint32_t SystemState  = 1 << 1;
            static const uint32_t AssetChanged = 1 << 2;
            static const uint32_t CoinsChanged = 1 << 3;
            static const uint32_t AddrsChanged = 1 << 4;
            static const uint32_t TXsChanged   = 1 << 5;
        };

        bool _subscribedToListener = false;
        SubFlags::Type _evSubs = 0;
        std::string _apiVersion;
        unsigned _apiVersionMajor;
        unsigned _apiVersionMinor;
        Wallet::Ptr _wallet;
    };
}
