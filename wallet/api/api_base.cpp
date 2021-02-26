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
    ApiBase::ApiBase(ACL acl)
        : _acl(std::move(acl))
    {
    }

    void ApiBase::onParseError(const json& msg)
    {
        LOG_DEBUG() << "onInvalidJsonRpc: " << msg;
        sendMessage(msg);
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
        sendMessage(error);
    }

    ApiBase::ParseJsonRes ApiBase::parseJSON(const char* data, size_t size)
    {
        {
            std::string s(data, size);
            static std::regex keyRE(R"'(\"key\"\s*:\s*\"[\d\w]+\"\s*,?)'");
            LOG_INFO() << "got " << std::regex_replace(s, keyRE, "");
        }

        if (size == 0)
        {
            const auto errEmptyJSON = formError(JsonRpcId(), ApiError::InvalidJsonRpc, "Empty JSON request.");
            onParseError(errEmptyJSON);
            return ParseJsonRes::ParseFail;
        }

        JsonRpcId id = JsonRpcId();

        try
        {
            json msg = json::parse(data, data + size);

            if(!msg["id"].is_number_integer() && !msg["id"].is_string())
            {
                throw jsonrpc_exception(ApiError::InvalidJsonRpc, "ID can be integer or string only.");
            }
            else
            {
                id = msg["id"];
            }

            if (msg[JsonRpcHeader] != JsonRpcVersion)
            {
                throw jsonrpc_exception(ApiError::InvalidJsonRpc, "Invalid JSON-RPC 2.0 header.");
            }

            if (_acl)
            {
                const json key = msg["key"];

                if (key.is_null())
                {
                    throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, "API key not specified.");
                }

                if (_acl->count(key) == 0)
                {
                    throw jsonrpc_exception(ApiError::UnknownApiKey, key);
                }
            }

            const auto method = getMandatoryParam<const JsonRpcId&>(msg, "method");
            if (_methods.find(method) == _methods.end())
            {
                throw jsonrpc_exception(ApiError::NotFoundJsonRpc, method);
            }

            bool isAsync = false;

            try
            {
                const auto& minfo = _methods[method];
                if(_acl && minfo.writeAccess && !_acl.get()[msg["key"]])
                {
                    throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc,"User doesn't have permissions to call this method.");
                }

                minfo.func(id, msg["params"] == nullptr ? json::object() : msg["params"]);
                isAsync = minfo.isAsync;
            }
            catch (const jsonrpc_exception&)
            {
                throw;
            }
            catch (const nlohmann::detail::exception& e)
            {
                LOG_ERROR() << "json parse: " << e.what() << "\n" << getJsonString(data, size);
                throw jsonrpc_exception(ApiError::InvalidJsonRpc, e.what());
            }
            catch (const std::runtime_error& e)
            {
                LOG_ERROR() << "error while calling " << method << ": " << e.what();
                throw jsonrpc_exception(ApiError::InternalErrorJsonRpc, e.what());
            }

            return isAsync ? ParseJsonRes::RunningAsync : ParseJsonRes::DoneSync;
        }
        catch (const jsonrpc_exception& e)
        {
            const auto error = formError(id, e.code(), e.whatstr());
            onParseError(error);
            return ParseJsonRes::DoneSync;
        }
        catch (const std::exception& e)
        {
            const auto error = formError(id, ApiError::InternalErrorJsonRpc, e.what());
            onParseError(error);
            return ParseJsonRes::DoneSync;
        }
        catch (...)
        {
            const auto error = formError(id, ApiError::InternalErrorJsonRpc, "API call failed, please take a look at logs");
            onParseError(error);
            return ParseJsonRes::DoneSync;
        }
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
                throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, "Parameter '" + name + "' must be a positive 64bit unsigned integer.");
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
