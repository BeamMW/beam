// Copyright 2023 The Beam Team
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
#include "v7_3_api.h"

namespace beam::wallet
{
    void V73Api::onHandleAssetsList(const JsonRpcId& id, AssetsList&& req)
    {
        if (!req.refresh)
        {
            AssetsList::Response resp;
            getWalletDB()->visitAssets([&resp](const auto& asset)
            {
                resp.assets.push_back(asset);
                return true;
            });
            doResponse(id, resp);
            return;
        }
        getWallet()->RequestAssetsListAt(req.height, [this, id, h = req.height, wguard = _weakSelf](auto&& assets)
        {
            auto guard = wguard.lock();
            if (!guard)
            {
                LOG_WARNING() << "API destroyed before shader response received.";
                return;
            }
            Height refreshHeight = h;
            Block::SystemState::ID stateID = {};
            getWalletDB()->getSystemStateID(stateID);
            if (h >= stateID.m_Height)
            {
                refreshHeight = stateID.m_Height;
            }
            AssetsList::Response resp;
            for (const auto& asset : assets)
            {
                resp.assets.push_back(WalletAsset(asset, refreshHeight));
            }
            doResponse(id, resp);
        });
    }
}
