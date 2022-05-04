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
#include "api_errors_imp.h"
#include "api_base.h"
#include "utility/logger.h"

namespace beam::wallet
{
    namespace
    {
        std::string CompactifyAddress(const std::string& str, size_t s)
        {
            const static std::string_view ellipsis = "...";
            if (str.size() <= s)
                return str;
            if (s <= ellipsis.size())
                return str.substr(0, s);

            auto s2 = s - ellipsis.size();
            auto h = (s2 / 2) + 1;
            std::string res;
            res.reserve(s);
            res.append(str, 0, h);
            res.append(ellipsis);
            res.append(str, h, s - h - ellipsis.size());
            return res;
        }

        void FilterRequest(json& root)
        {
            for (auto& item : root.items())
            {
                if (item.value().is_object())
                {
                    FilterRequest(item.value());
                }
                else if (item.value().is_string() && item.key() == "address")
                {
                    root[item.key()] = CompactifyAddress(item.value().get<std::string>(), 32);
                }
                else if (item.value().is_array() && item.key() == "contract")
                {
                    root[item.key()] = json::array();
                }
            }
        }
    }

    ApiBase::ApiBase(IWalletApiHandler& handler, const ApiInitData& initData)
        : _handler(handler)
        , _acl(initData.acl)
        , _appId(initData.appId)
        , _appName(initData.appName)
    {
        // MUST BE SAFE TO CALL FROM ANY THREAD
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

    std::string ApiBase::fromError(const std::string& request, ApiError code, const std::string& errorText)
    {
        JsonRpcId rpcId;

        try
        {
            const auto parsed = json::parse(request);
            rpcId = parsed["id"];
        }
        catch(...)
        {
            LOG_WARNING() << "ApiBase::fromError - failed to parse request, " << request;
        }

        const auto err = formError(rpcId, code, errorText);
        return err.dump();
    }

    void ApiBase::sendError(const JsonRpcId& id, ApiError code, const std::string& data)
    {
        const auto error = formError(id, code, data);
        _handler.sendAPIResponse(error);
    }

    boost::optional<IWalletApi::ApiCallInfo> ApiBase::parseCallInfo(const char* data, size_t size)
    {
        JsonRpcId rpcid;
        return callGuarded<ApiCallInfo>(rpcid, [this, &rpcid, data, size] () {
            if (size == 0)
            {
                throw jsonrpc_exception(ApiError::InvalidJsonRpc, "Empty JSON request");
            }

            ApiCallInfo info;

            info.message = json::parse(data, data + size); // do not make const pls, it would throw if no field present
            if(!info.message["id"].is_number_integer() && !info.message["id"].is_string())
            {
                throw jsonrpc_exception(ApiError::InvalidJsonRpc, "ID can be integer or string only.");
            }

            info.rpcid = info.message["id"];
            rpcid = info.rpcid;

            if (info.message[JsonRpcHeader] != JsonRpcVersion)
            {
                throw jsonrpc_exception(ApiError::InvalidJsonRpc, "Invalid JSON-RPC 2.0 header.");
            }

            info.method = getMandatoryParam<NonEmptyString>(info.message, "method");
            if (_methods.find(info.method) == _methods.end())
            {
                throw jsonrpc_exception(ApiError::NotFoundJsonRpc, info.method);
            }

            const auto& minfo = _methods[info.method];

            info.params = info.message["params"];
            info.appsAllowed = minfo.appsAllowed;

            return info;
        });
    }

    boost::optional<IWalletApi::ParseResult> ApiBase::parseAPIRequest(const char* data, size_t size)
    {
        const auto pinfo = parseCallInfo(data, size);
        if (pinfo == boost::none)
        {
            return boost::none;
        }

        return callGuarded<ParseResult>(pinfo->rpcid, [this, &pinfo] () -> ParseResult {
            const auto& minfo = _methods[pinfo->method];
            const auto finfo = minfo.parseFunc(pinfo->rpcid, pinfo->params);

            ParseResult result(*pinfo, finfo);
            return result;
        });
    }

    ApiSyncMode ApiBase::executeAPIRequest(const char* data, size_t size)
    {
        auto pinfo = parseCallInfo(data, size);
        if (pinfo == boost::none)
        {
            LOG_WARNING() << "executeAPIRequest, parseCallInfo returned none for " << std::string_view(data, size);
            return ApiSyncMode::DoneSync;
        }

        {
            json messageCopy = pinfo->message;
            FilterRequest(messageCopy);

            const auto message = messageCopy.dump(1, '\t');
            LOG_VERBOSE() << "executeAPIRequest:\n" << message;
        }

        const auto result = callGuarded<ApiSyncMode>(pinfo->rpcid, [this, &pinfo] () -> ApiSyncMode {
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

            minfo.execFunc(pinfo->rpcid, pinfo->params);
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

    bool ApiBase::hasParam(const json& params, const std::string& name)
    {
        return params.find(name) != params.end();
    }
}
