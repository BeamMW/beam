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
#include "api_errors.h"
#include "api_base.h"
#include "utility/logger.h"
#include <regex>

// TODO: check ranges for is_number_unsigned
namespace {
    std::string getJsonString(const char* data, size_t size)
    {
        return std::string(data, data + (size > 1024 ? 1024 : size));
    }
}

namespace beam::wallet {
    ApiBase::ApiBase(IWalletApiHandler& handler, ACL acl)
        : _acl(std::move(acl))
        , _handler(handler)
    {
    }

    json ApiBase::formError(const JsonRpcId& id, ApiError code, const std::string& data)
    {
        auto error = json {
            {JsonRpcHeader, JsonRpcVersion},
            {"error",
                {
                    {"code", code},
                    {"message", getApiErrorMessage(code)}
                }
            }
        };

        if (id.is_number_integer() || id.is_string())
        {
            error["id"] = id;
        }

        if (!data.empty())
        {
            error["error"]["data"] = data;
        }

        return error;
    }

    void ApiBase::sendError(const JsonRpcId& id, ApiError code, const std::string& data)
    {
        const auto error = formError(id, code, data);
        _handler.sendAPIResponse(error);
    }

    boost::optional<IWalletApi::ParseInfo> ApiBase::parseAPIRequest(const char* data, size_t size)
    {
        JsonRpcId rpcid;
        return callGuarded<IWalletApi::ParseInfo>(rpcid, [this, &rpcid, data, size] () -> IWalletApi::ParseInfo {
            {
                std::string s(data, size);
                static std::regex keyRE(R"'(\"key\"\s*:\s*\"[\d\w]+\"\s*,?)'");
                LOG_INFO() << "got " << std::regex_replace(s, keyRE, "");
            }

            if (size == 0)
            {
                throw jsonrpc_exception(ApiError::InvalidJsonRpc, "Empty JSON request");
            }

            ParseInfo result;
            result.message = json::parse(data, data + size);

            if(!result.message["id"].is_number_integer() && !result.message["id"].is_string())
            {
                throw jsonrpc_exception(ApiError::InvalidJsonRpc, "ID can be integer or string only.");
            }

            result.rpcid = result.message["id"];
            rpcid = result.rpcid;

            if (result.message[JsonRpcHeader] != JsonRpcVersion)
            {
                throw jsonrpc_exception(ApiError::InvalidJsonRpc, "Invalid JSON-RPC 2.0 header.");
            }

            result.method = getMandatoryParam<NonEmptyString>(result.message, "method");
            if (_methods.find(result.method) == _methods.end())
            {
                throw jsonrpc_exception(ApiError::NotFoundJsonRpc, result.method);
            }

            result.params = result.message["params"];
            return result;
        });
    }

    ApiSyncMode ApiBase::executeAPIRequest(const char* data, size_t size)
    {
        const auto pinfo = parseAPIRequest(data, size);
        if (pinfo == boost::none)
        {
            return ApiSyncMode::DoneSync;
        }

        const auto result = callGuarded<ApiSyncMode>(pinfo->rpcid, [this, pinfo] () -> ApiSyncMode {
            const auto& minfo = _methods[pinfo->method];

            if (_acl)
            {
                const std::string key = getMandatoryParam<NonEmptyString>(pinfo->message, "key");
                if (_acl->count(key) == 0)
                {
                    throw jsonrpc_exception(ApiError::UnknownApiKey, key);
                }

                if(minfo.writeAccess && !_acl.get()[key])
                {
                    throw jsonrpc_exception(ApiError::InternalErrorJsonRpc,"User doesn't have permissions to call this method.");
                }
            }

            minfo.func(pinfo->rpcid, pinfo->params);
            return minfo.isAsync ? ApiSyncMode::RunningAsync : ApiSyncMode::DoneSync;
        });

        return result ? *result : ApiSyncMode::DoneSync;
    }

    template<>
    boost::optional<const json&> ApiBase::getOptionalParam<const json&>(const json &params, const std::string &name)
    {
        const auto it = params.find(name);
        if(it != params.end())
        {
            return boost::optional<const json&>(it.value());
        }
        return boost::none;
    }

    template<>
    boost::optional<std::string> ApiBase::getOptionalParam<std::string>(const json &params, const std::string &name)
    {
        if(auto raw = getOptionalParam<const json&>(params, name))
        {
            if (!(*raw).is_string())
            {
                throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, "Parameter '" + name + "' must be a string.");
            }
            return (*raw).get<std::string>();
        }
        return boost::none;
    }

    template<>
    boost::optional<NonEmptyString> ApiBase::getOptionalParam<NonEmptyString>(const json &params, const std::string &name)
    {
        if (auto raw = getOptionalParam<std::string>(params, name))
        {
            auto result = *raw;
            if (result.empty())
            {
                throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc,"Parameter '" + name + "' must be a non-empty string.");
            }
            return NonEmptyString(result);
        }
        return boost::none;
    }

    template<>
    boost::optional<bool> ApiBase::getOptionalParam<bool>(const json &params, const std::string &name)
    {
        if(auto oraw = getOptionalParam<const json&>(params, name))
        {
            const auto &raw = *oraw;
            if (!raw.is_boolean())
            {
                throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, "Parameter '" + name + "' must be bool.");
            }

            return raw.get<bool>();
        }
        return boost::none;
    }

    template<>
    boost::optional<JsonArray> ApiBase::getOptionalParam<JsonArray>(const json &params, const std::string &name)
    {
        if(auto raw = getOptionalParam<const json&>(params, name))
        {
            const json& arr = *raw;
            if (!arr.is_array())
            {
                throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, "Parameter '" + name + "' must be an array.");
            }
            return JsonArray(arr);
        }
        return boost::none;
    }

    template<>
    boost::optional<NonEmptyJsonArray> ApiBase::getOptionalParam<NonEmptyJsonArray>(const json &params, const std::string &name)
    {
        if (auto raw = getOptionalParam<JsonArray>(params, name))
        {
            const json& arr = *raw;
            if (arr.empty())
            {
                throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, "Parameter '" + name + "' must be a non-empty array.");
            }
            return NonEmptyJsonArray(arr);
        }
        return boost::none;
    }

    template<>
    boost::optional<uint32_t> ApiBase::getOptionalParam<uint32_t>(const json &params, const std::string &name)
    {
        if(auto oraw = getOptionalParam<const json&>(params, name))
        {
            const auto& raw = *oraw;
            if (!raw.is_number_unsigned())
            {
                throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, "Parameter '" + name + "' must be an unsigned integer.");
            }
            return raw.get<uint32_t>();
        }
        return boost::none;
    }

    template<>
    boost::optional<PositiveUint32> ApiBase::getOptionalParam<PositiveUint32>(const json &params, const std::string &name)
    {
        if(auto raw = getOptionalParam<uint32_t>(params, name))
        {
            const auto result = *raw;
            if (result == 0)
            {
                throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, "Parameter '" + name + "' must be a positive 32bit unsigned integer.");
            }

            return PositiveUint32(result);
        }
        return boost::none;
    }

    template<>
    boost::optional<uint64_t> ApiBase::getOptionalParam<uint64_t>(const json &params, const std::string &name)
    {
        if(auto oraw = getOptionalParam<const json&>(params, name))
        {
            const auto& raw = *oraw;
            if (!raw.is_number_unsigned())
            {
                throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, "Parameter '" + name + "' must be an unsigned integer.");
            }
            return raw.get<uint64_t>();
        }
        return boost::none;
    }

    template<>
    boost::optional<PositiveUnit64> ApiBase::getOptionalParam<PositiveUnit64>(const json &params, const std::string &name)
    {
        if(auto raw = getOptionalParam<uint64_t>(params, name))
        {
            auto result = *raw;
            if (result == 0)
            {
                throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, "Parameter '" + name + "' must be a positive 64bit unsigned integer.");
            }

            return PositiveUnit64(result);
        }
        return boost::none;
    }

    template<>
    boost::optional<PositiveAmount> ApiBase::getOptionalParam<PositiveAmount>(const json &params, const std::string &name)
    {
        if (auto amount = getOptionalParam<PositiveUnit64>(params, name))
        {
            return PositiveAmount(*amount);
        }
        return boost::none;
    }

    template<>
    boost::optional<PositiveHeight> ApiBase::getOptionalParam<PositiveHeight>(const json &params, const std::string &name)
    {
        if (auto height = getOptionalParam<PositiveUnit64>(params, name))
        {
            return PositiveHeight(*height);
        }
        return boost::none;
    }

    bool ApiBase::hasParam(const json& params, const std::string& name)
    {
        return params.find(name) != params.end();
    }

    template<>
    boost::optional<ValidTxID> ApiBase::getOptionalParam<ValidTxID>(const json &params, const std::string &name)
    {
        if (auto raw = getOptionalParam<NonEmptyString>(params, name))
        {
            bool isValid = false;
            auto txid = from_hex(*raw, &isValid);

            if (!isValid || txid.size() != TxID().size())
            {
                throw jsonrpc_exception(ApiError::InvalidTxId, "Transaction ID has wrong format.");
            }

            TxID result;
            std::copy_n(txid.begin(), TxID().size(), result.begin());

            return ValidTxID(result);
        }
        return boost::none;
    }
}
