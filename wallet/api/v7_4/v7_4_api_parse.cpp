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
#include "v7_4_api.h"
#include "version.h"
#include "wallet/core/base58.h"

namespace beam::wallet
{
std::pair<SendSbbsMessage, IWalletApi::MethodInfo> V74Api::onParseSendSbbsMessage(const JsonRpcId& id, const nlohmann::json& params)
{
    SendSbbsMessage message;
    message.receiver.FromBuf(getMandatoryParam<ValidHexBuffer>(params, "receiver"));
    if (auto sender = getOptionalParam<ValidHexBuffer>(params, "sender"))
    {
        message.sender.FromBuf(*sender);
    }
    else
    {
        WalletAddress wa;
        getWalletDB()->getDefaultAddressAlways(wa);
        message.sender = wa.m_BbsAddr;
    }

    json jsonMessage = getMandatoryParam<JsonObject>(params, "message");

    auto messageDump = jsonMessage.dump();
    ByteBuffer messageBuffer(messageDump.begin(), messageDump.end());
    message.message = messageBuffer;

    return std::make_pair(std::move(message), MethodInfo());
}

void V74Api::getResponse(const JsonRpcId& id, const SendSbbsMessage::Response& res, json& msg)
{
    msg = json
    {
        {JsonRpcHeader, JsonRpcVersion},
        {"id", id},
        {"result",
            {
                {"from", std::to_string(res.sender)},
                {"to", std::to_string(res.receiver)},
                {"bytes", res.bytes}
            }
        }
    };
}

std::pair<ReadSbbsMessages, IWalletApi::MethodInfo> V74Api::onParseReadSbbsMessages(const JsonRpcId& id, const nlohmann::json& params)
{
    ReadSbbsMessages message;
    if (auto all = getOptionalParam<bool>(params, "all"))
    {
        message.all = *all;
    }
    return std::make_pair(std::move(message), MethodInfo());
}

void V74Api::getResponse(const JsonRpcId& id, const ReadSbbsMessages::Response& res, json& msg)
{
    msg = json
    {
        {JsonRpcHeader, JsonRpcVersion},
        {"id", id},
        {"result", json::array()}
    };
    for (auto& message : res.messages)
    {
        msg["result"].push_back(
            {
                {"id", message.m_id},
                {"timestamp", message.m_timestamp},
                {"sender", std::to_string(message.m_counterpart)},
                {"message", json::parse(message.m_message)}
            });
    }
}
}
