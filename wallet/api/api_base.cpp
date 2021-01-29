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

namespace {
    using namespace beam::wallet;

    std::string getJsonString(const char* data, size_t size)
    {
        return std::string(data, data + (size > 1024 ? 1024 : size));
    }

    const auto errEmptyJSON = json {
        {ApiBase::JsonRpcHeader, ApiBase::JsonRpcVersion},
        {"error",
            {
                {"code", ApiError::InvalidJsonRpc},
                {"message", "Empty JSON request."},
            }
        }
    };
}

namespace beam::wallet {
    ApiBase::ApiBase(IApiBaseHandler& handler, ACL acl)
        : _handler(handler)
        , _acl(std::move(acl))
    {
    }

    bool ApiBase::parseJSON(const char* data, size_t size)
    {
        {
            std::string s(data, size);
            static std::regex keyRE(R"'(\"key\"\s*:\s*\"[\d\w]+\"\s*,?)'");
            LOG_INFO() << "got " << std::regex_replace(s, keyRE, "");
        }

        if (size == 0)
        {
            _handler.onRPCError(errEmptyJSON);
            return false;
        }

        try
        {
            json msg = json::parse(data, data + size);

            const JsonRpcId id = msg["id"];
            if(!id.is_number_integer() && !id.is_string())
            {
                throw jsonrpc_exception(ApiError::InvalidJsonRpc, JsonRpcId(), "ID can be integer or string only.");
            }

            if (msg[JsonRpcHeader] != JsonRpcVersion)
            {
                throw jsonrpc_exception(ApiError::InvalidJsonRpc, id, "Invalid JSON-RPC 2.0 header.");
            }

            if (_acl)
            {
                const json key = msg["key"];

                if (key.is_null())
                {
                    throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, id, "API key not specified.");
                }

                if (_acl->count(key) == 0)
                {
                    throw jsonrpc_exception(ApiError::UnknownApiKey, id,key);
                }
            }

            const auto method = getMandatoryParam<const JsonRpcId&>(msg, "method", id);
            if (_methods.find(method) == _methods.end())
            {
                throw jsonrpc_exception(ApiError::NotFoundJsonRpc, id, method);
            }

            try
            {
                const auto& minfo = _methods[method];
                if(_acl && minfo.writeAccess && !_acl.get()[msg["key"]])
                {
                    throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, id, "User doesn't have permissions to call this method.");
                }

                minfo.func(id, msg["params"] == nullptr ? json::object() : msg["params"]);
            }
            catch (const nlohmann::detail::exception& e)
            {
                LOG_ERROR() << "json parse: " << e.what() << "\n" << getJsonString(data, size);
                throw jsonrpc_exception{ ApiError::InvalidJsonRpc , e.what(), id };
            }
            catch (const jsonrpc_exception&)
            {
                throw;
            }
            catch (const std::runtime_error& e)
            {
                LOG_ERROR() << "error while calling " << method << ": " << e.what();
                throw jsonrpc_exception{ApiError::InternalErrorJsonRpc, e.what(), id};
            }
        }
        catch (const jsonrpc_exception& e)
        {
            json msg
            {
                {JsonRpcHeader, JsonRpcVersion},
                {"error",
                    {
                        {"code", e.code()},
                        {"message", getErrorMessage(e.code())},
                    }
                }
            };

            if (e.has_what())
            {
                msg["error"]["data"] = e.whatstr();
            }

            if (e.rpcid().is_number_integer() || e.rpcid().is_string())
            {
                msg["id"] = e.rpcid();
            }

            _handler.onRPCError(msg);
        }
        catch (const std::exception& e)
        {
            json msg
            {
                {JsonRpcHeader, JsonRpcVersion},
                {"error",
                    {
                        {"code", ApiError::InternalErrorJsonRpc},
                        {"message", e.what()},
                    }
                }
            };

            _handler.onRPCError(msg);
        }

        return true;
    }

    template<>
    const json& ApiBase::getMandatoryParam<const json&>(const json &params, const std::string &name, const JsonRpcId &id)
    {
        const auto it = params.find(name);
        if (it == params.end())
        {
            throwParameterAbsence(id, name);
        }
        return it.value();
    }

    template<>
    std::string ApiBase::getMandatoryParam<std::string>(const json &params, const std::string &name, const JsonRpcId &id)
    {
        auto raw = getMandatoryParam<const json&>(params, name, id);
        if (!raw.is_string())
        {
            throw jsonrpc_exception(ApiError::InvalidJsonRpc, "Parameter '" + name + "' must be a string.", id);
        }

        auto result = raw.get<std::string>();
        if (result.empty())
        {
            throw jsonrpc_exception(ApiError::InvalidJsonRpc, "Parameter '" + name + "' must be a non-empty string.", id);
        }

        return result;
    }

    template<>
    bool ApiBase::getMandatoryParam<bool>(const json &params, const std::string &name, const JsonRpcId &id)
    {
        auto raw = getMandatoryParam<const json&>(params, name, id);
        if (!raw.is_boolean())
        {
            throw jsonrpc_exception(ApiError::InvalidJsonRpc, "Parameter '" + name + "' must be bool.", id);
        }

        auto result = raw.get<bool>();
        return result;
    }

    template<>
    JsonArray ApiBase::getMandatoryParam<JsonArray>(const json &params, const std::string &name, const JsonRpcId &id)
    {
        auto raw = getMandatoryParam<const json&>(params, name, id);
        if (!raw.is_array())
        {
            throw jsonrpc_exception(ApiError::InvalidJsonRpc, "Parameter '" + name + "' must be an array.", id);
        }

        if (raw.empty())
        {
            throw jsonrpc_exception(ApiError::InvalidJsonRpc, "Parameter '" + name + "' must be a non-empty array.", id);
        }

        return JsonArray(raw);
    }

    template<>
    PositiveUnit64 ApiBase::getMandatoryParam<PositiveUnit64>(const json &params, const std::string &name, const JsonRpcId &id)
    {
        auto raw = getMandatoryParam<const json&>(params, name, id);
        if (!raw.is_number_unsigned())
        {
            throw jsonrpc_exception(ApiError::InvalidJsonRpc, "Parameter '" + name + "' must be a 64bit unsigned integer.", id);
        }

        auto result = raw.get<uint64_t>();
        if (result == 0)
        {
            throw jsonrpc_exception(ApiError::InvalidJsonRpc, "Parameter '" + name + "' must be a positive 64bit unsigned integer.", id);
        }

        return PositiveUnit64(result);
    }

    template<>
    uint32_t ApiBase::getMandatoryParam<uint32_t>(const json &params, const std::string &name, const JsonRpcId &id)
    {
        auto raw = getMandatoryParam<const json&>(params, name, id);
        if (!raw.is_number_unsigned())
        {
            throw jsonrpc_exception(ApiError::InvalidJsonRpc, "Parameter '" + name + "' must be an unsigned integer.", id);
        }

        return raw.get<uint32_t>();
    }
}
