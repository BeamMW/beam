// Copyright 2022 The Beam Team
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
#include "v7_2_api.h"
#include "version.h"

namespace beam::wallet
{
#ifdef BEAM_ASSET_SWAP_SUPPORT
std::pair<AssetsSwapOffersList, IWalletApi::MethodInfo> V72Api::onParseAssetsSwapOffersList(const JsonRpcId& id, const nlohmann::json& params)
{
    AssetsSwapOffersList message;
    return std::make_pair(std::move(message), MethodInfo());
}

void V72Api::getResponse(const JsonRpcId& id, const AssetsSwapOffersList::Response& res, json& msg)
{
    msg = json
    {
        {JsonRpcHeader, JsonRpcVersion},
        {"id", id}
    };

    msg["offers"] = json::array();
    auto& joffers = msg["offers"];

    for (auto& order: res.orders)
    {
        auto jorded = json
        {
            {"id", order.getID().to_string()},
            {"sendAmount", order.getSendAmount()},
            {"sendCurrencyName", order.getSendAssetSName()},
            {"sendAssetId", order.getSendAssetId()},
            {"receiveAmount", order.getReceiveAmount()},
            {"receiveCurrencyName", order.getReceiveAssetSName()},
            {"receiveAssetId", order.getReceiveAssetId()},
            {"create_time", order.getCreation()},
            {"expire_time", order.getExpiration()},
            {"isMy", order.isMine()}
        };

        joffers.emplace_back(std::move(jorded));
    }
}

std::pair<AssetsSwapCreate, IWalletApi::MethodInfo> V72Api::onParseAssetsSwapCreate(const JsonRpcId& id, const nlohmann::json& params)
{
    AssetsSwapCreate message;

    message.sendAmount = getMandatoryParam<PositiveAmount>(params, "send_amount");
    message.sendAsset = getMandatoryParam<uint32_t>(params, "send_asset_id");
    message.receiveAmount = getMandatoryParam<PositiveAmount>(params, "receive_amount");
    message.receiveAsset = getMandatoryParam<uint32_t>(params, "receive_asset_id");
    message.expireMinutes = getMandatoryParam<uint32_t>(params, "minutes_before_expire");

    auto comment = getOptionalParam<std::string>(params, "comment");
    if (comment)
        message.comment = *comment;

    return std::make_pair(std::move(message), MethodInfo());
}

void V72Api::getResponse(const JsonRpcId& id, const AssetsSwapCreate::Response& res, json& msg)
{
    const auto& order = res.order;
    msg = json
    {
        {JsonRpcHeader, JsonRpcVersion},
        {"id", id},
        {"offer", {
            {"id", order.getID().to_string()},
            {"sendAmount", order.getSendAmount()},
            {"sendCurrencyName", order.getSendAssetSName()},
            {"sendAssetId", order.getSendAssetId()},
            {"receiveAmount", order.getReceiveAmount()},
            {"receiveCurrencyName", order.getReceiveAssetSName()},
            {"receiveAssetId", order.getReceiveAssetId()},
            {"create_time", order.getCreation()},
            {"expire_time", order.getExpiration()},
            {"isMy", order.isMine()}
        }}
    };
}

std::pair<AssetsSwapCancel, IWalletApi::MethodInfo> V72Api::onParseAssetsSwapCancel(const JsonRpcId& id, const nlohmann::json& params)
{
    AssetsSwapCancel message;
    message.offerId = getMandatoryParam<std::string>(params, "offer_id");
    return std::make_pair(std::move(message), MethodInfo());
}

void V72Api::getResponse(const JsonRpcId& id, const AssetsSwapCancel::Response& res, json& msg)
{
    msg = json
    {
        {JsonRpcHeader, JsonRpcVersion},
        {"id", id},
        {"offer", res.offerId.to_string()}
    };
}

std::pair<AssetsSwapAccept, IWalletApi::MethodInfo> V72Api::onParseAssetsSwapAccept(const JsonRpcId& id, const nlohmann::json& params)
{
    AssetsSwapAccept message;
    message.offerId = getMandatoryParam<std::string>(params, "offer_id");
    auto comment = getOptionalParam<std::string>(params, "comment");
    if (comment)
        message.comment = *comment;
    return std::make_pair(std::move(message), MethodInfo());
}

void V72Api::getResponse(const JsonRpcId& id, const AssetsSwapAccept::Response& res, json& msg)
{
    const auto& order = res.order;

    msg = json
    {
        {JsonRpcHeader, JsonRpcVersion},
        {"id", id},
        {"tx_id", std::to_string(res.txId)},
        {"offer", {
            {"id", order.getID().to_string()},
            {"sendAmount", order.getSendAmount()},
            {"sendCurrencyName", order.getSendAssetSName()},
            {"sendAssetId", order.getSendAssetId()},
            {"receiveAmount", order.getReceiveAmount()},
            {"receiveCurrencyName", order.getReceiveAssetSName()},
            {"receiveAssetId", order.getReceiveAssetId()},
            {"create_time", order.getCreation()},
            {"expire_time", order.getExpiration()},
            {"isMy", order.isMine()}
        }}
    };
}

#endif  // BEAM_ASSET_SWAP_SUPPORT
}
