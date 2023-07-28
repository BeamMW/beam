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
#include "version.h"

namespace beam::wallet
{
std::pair<AssetsList, IWalletApi::MethodInfo> V73Api::onParseAssetsList(const JsonRpcId& id, const nlohmann::json& params)
{
    LOG_DEBUG() << __FUNCTION__;
    AssetsList message;
    if (auto refresh = getOptionalParam<bool>(params, "refresh"))
    {
        message.refresh = *refresh;
    }
    if (auto height = getOptionalParam<Height>(params, "height"))
    {
        message.height = *height;
    }
    return std::make_pair(std::move(message), MethodInfo());
}

void V73Api::getResponse(const JsonRpcId& id, const AssetsList::Response& res, json& msg)
{
    msg = json
    {
        {JsonRpcHeader, JsonRpcVersion},
        {"id", id}
    };

    msg["assets"] = json::array();
    auto& jsonAssets = msg["assets"];

    LOG_DEBUG() << __FUNCTION__ << " assets: " << jsonAssets.size();

    for (auto& asset: res.assets)
    {
        auto jsonAsset = json();
        fillAssetInfo(jsonAsset, asset);
        jsonAssets.emplace_back(std::move(jsonAsset));
    }
}
}
