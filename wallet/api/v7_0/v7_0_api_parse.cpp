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
#include "v7_0_api.h"
#include "version.h"

namespace beam::wallet
{
    namespace
    {
        uint32_t parseTimeout(V70Api& api, const nlohmann::json& params)
        {
            if(auto otimeout = api.getOptionalParam<uint32_t>(params, "timeout"))
            {
                return *otimeout;
            }
            return 0;
        }

        bool ExtractPoint(ECC::Point::Native& point, const json& j)
        {
            auto s = type_get<NonEmptyString>(j);
            auto buf = from_hex(s);
            ECC::Point pt;
            Deserializer dr;
            dr.reset(buf);
            dr& pt;

            return point.ImportNnz(pt);
        }
    }

    template<>
    const char* type_name<ECC::Point::Native>()
    {
        return "hex encoded elliptic curve point";
    }

    template<>
    bool type_check<ECC::Point::Native>(const json& j)
    {
        ECC::Point::Native pt;
        return type_check<NonEmptyString>(j) && ExtractPoint(pt, j);
    }

    template<>
    ECC::Point::Native type_get<ECC::Point::Native>(const json& j)
    {
        ECC::Point::Native pt;
        ExtractPoint(pt, j);
        return pt;
    }

    std::pair<IPFSAdd, IWalletApi::MethodInfo> V70Api::onParseIPFSAdd(const JsonRpcId& id, const nlohmann::json& params)
    {
        IPFSAdd message;
        message.timeout = parseTimeout(*this, params);

        json data = getMandatoryParam<NonEmptyJsonArray>(params, "data");
        data.get<std::vector<uint8_t>>().swap(message.data);

        if (auto opin = getOptionalParam<bool>(params, "pin"))
        {
            message.pin = *opin;
        }

        return std::make_pair(std::move(message), MethodInfo());
    }

    void V70Api::getResponse(const JsonRpcId& id, const IPFSAdd::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result",
                {
                    {"hash", res.hash},
                    {"pinned", res.pinned}
                }
            }
        };
    }

    std::pair<IPFSHash, IWalletApi::MethodInfo> V70Api::onParseIPFSHash(const JsonRpcId& id, const nlohmann::json& params)
    {
        IPFSHash message;
        message.timeout = parseTimeout(*this, params);

        json data = getMandatoryParam<NonEmptyJsonArray>(params, "data");
        data.get<std::vector<uint8_t>>().swap(message.data);

        return std::make_pair(std::move(message), MethodInfo());
    }

    void V70Api::getResponse(const JsonRpcId& id, const IPFSHash::Response& res, json& msg)
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

    std::pair<IPFSGet, IWalletApi::MethodInfo> V70Api::onParseIPFSGet(const JsonRpcId& id, const nlohmann::json& params)
    {
        IPFSGet message;
        message.timeout = parseTimeout(*this, params);
        message.hash = getMandatoryParam<NonEmptyString>(params, "hash");
        return std::make_pair(std::move(message), MethodInfo());
    }

    void V70Api::getResponse(const JsonRpcId& id, const IPFSGet::Response& res, json& msg)
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

    std::pair<IPFSPin, IWalletApi::MethodInfo> V70Api::onParseIPFSPin(const JsonRpcId& id, const nlohmann::json& params)
    {
        IPFSPin message;
        message.timeout = parseTimeout(*this, params);
        message.hash = getMandatoryParam<NonEmptyString>(params, "hash");
        return std::make_pair(std::move(message), MethodInfo());
    }

    void V70Api::getResponse(const JsonRpcId& id, const IPFSPin::Response& res, json& msg)
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

    std::pair<IPFSUnpin, IWalletApi::MethodInfo> V70Api::onParseIPFSUnpin(const JsonRpcId& id, const nlohmann::json& params)
    {
        IPFSUnpin message;
        message.hash = getMandatoryParam<NonEmptyString>(params, "hash");
        return std::make_pair(std::move(message), MethodInfo());
    }

    void V70Api::getResponse(const JsonRpcId& id, const IPFSUnpin::Response& res, json& msg)
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

    std::pair<IPFSGc, IWalletApi::MethodInfo> V70Api::onParseIPFSGc(const JsonRpcId& id, const nlohmann::json& params)
    {
        IPFSGc message;
        message.timeout = parseTimeout(*this, params);
        return std::make_pair(message, MethodInfo());
    }

    void V70Api::getResponse(const JsonRpcId& id, const IPFSGc::Response& res, json& msg)
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

    std::pair<SignMessage, IWalletApi::MethodInfo> V70Api::onParseSignMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        SignMessage message;
        message.message = getMandatoryParam<NonEmptyString>(params, "message");
        auto km = getMandatoryParam<NonEmptyString>(params, "key_material");
        message.keyMaterial = from_hex(km);
        return std::make_pair(message, MethodInfo());
    }

    void V70Api::getResponse(const JsonRpcId& id, const SignMessage::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result",
                {
                    {"signature", res.signature}
                }
            }
        };
    }

    std::pair<VerifySignature, IWalletApi::MethodInfo> V70Api::onParseVerifySignature(const JsonRpcId& id, const nlohmann::json& params)
    {
        VerifySignature message;
        message.message = getMandatoryParam<NonEmptyString>(params, "message");
        message.publicKey = getMandatoryParam<ECC::Point::Native>(params, "public_key");
        message.signature = getMandatoryParam<ValidHexBuffer>(params, "signature");
        
        return std::make_pair(message, MethodInfo());
    }

    void V70Api::getResponse(const JsonRpcId& id, const VerifySignature::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", res.result }
        };
    }
}
