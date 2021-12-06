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
#include "v6_3_api.h"
#include "version.h"

namespace beam::wallet
{
    namespace
    {
        uint32_t parseTimeout(V63Api& api, const nlohmann::json& params)
        {
            if(auto otimeout = api.getOptionalParam<uint32_t>(params, "timeout"))
            {
                return *otimeout;
            }
            return 0;
        }
    }

    std::pair<IPFSAdd, IWalletApi::MethodInfo> V63Api::onParseIPFSAdd(const JsonRpcId& id, const nlohmann::json& params)
    {
        IPFSAdd message;

        // TODO:IPFS json&, optimize? this might be slow, BSON?
        json data = getMandatoryParam<NonEmptyJsonArray>(params, "data");
        for (const auto& byte: data)
        {
            const auto ubyte = byte.get<uint8_t>();
            message.data.push_back(ubyte);
        }

        return std::make_pair(std::move(message), MethodInfo());
    }

    void V63Api::getResponse(const JsonRpcId& id, const IPFSAdd::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result",
                {
                    {"hash", res.hash}
                }
            }
        };
    }

    std::pair<IPFSGet, IWalletApi::MethodInfo> V63Api::onParseIPFSGet(const JsonRpcId& id, const nlohmann::json& params)
    {
        IPFSGet message;
        message.timeout = parseTimeout(*this, params);
        message.hash = getMandatoryParam<NonEmptyString>(params, "hash");
        return std::make_pair(std::move(message), MethodInfo());
    }

    void V63Api::getResponse(const JsonRpcId& id, const IPFSGet::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result",
                {
                    {"hash", res.hash},
                    {"data", res.data}
                }
            }
        };
    }

    std::pair<IPFSPin, IWalletApi::MethodInfo> V63Api::onParseIPFSPin(const JsonRpcId& id, const nlohmann::json& params)
    {
        IPFSPin message;
        message.timeout = parseTimeout(*this, params);
        message.hash = getMandatoryParam<NonEmptyString>(params, "hash");
        return std::make_pair(std::move(message), MethodInfo());
    }

    void V63Api::getResponse(const JsonRpcId& id, const IPFSPin::Response& res, json& msg)
    {
        msg = json
            {
                {JsonRpcHeader, JsonRpcVersion},
                {"id", id},
                {"result",
                    {
                        {"hash", res.hash},
                        {"result", true}
                    }
                }
            };
    }

    std::pair<IPFSUnpin, IWalletApi::MethodInfo> V63Api::onParseIPFSUnpin(const JsonRpcId& id, const nlohmann::json& params)
    {
        IPFSUnpin message;
        message.timeout = parseTimeout(*this, params);
        message.hash = getMandatoryParam<NonEmptyString>(params, "hash");
        return std::make_pair(std::move(message), MethodInfo());
    }

    void V63Api::getResponse(const JsonRpcId& id, const IPFSUnpin::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result",
                {
                    {"hash", res.hash},
                    {"result", true}
                }
            }
        };
    }

    std::pair<IPFSGc, IWalletApi::MethodInfo> V63Api::onParseIPFSGc(const JsonRpcId& id, const nlohmann::json& params)
    {
        IPFSGc message;
        message.timeout = parseTimeout(*this, params);
        return std::make_pair(message, MethodInfo());
    }

    void V63Api::getResponse(const JsonRpcId& id, const IPFSGc::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result",
                {
                    {"result", true}
                }
            }
        };
    }
}
