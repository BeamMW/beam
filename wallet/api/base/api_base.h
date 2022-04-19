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
#pragma once

#include "api_errors_imp.h"
#include "wallet/core/common.h"
#include "utility/common.h"
#include "../i_wallet_api.h"
#include "parse_utils.h"

namespace beam::wallet
{
    #define API_WRITE_ACCESS true
    #define API_READ_ACCESS false
    #define API_ASYNC true
    #define API_SYNC false
    #define APPS_ALLOWED true
    #define APPS_BLOCKED false

    #define BEAM_API_RESPONSE_FUNC(api, name, ...) \
        void getResponse(const JsonRpcId& id, const api::Response& data, json& msg); \
        void doResponse(const JsonRpcId& id, const api::Response& response) \
        {\
            json msg; \
            getResponse(id, response, msg); \
            LOG_VERBOSE() << "Api call result for id " << id; \
            LOG_VERBOSE() << "\tresponse: " << std::string_view(msg.dump(1, '\t')); \
            _handler.sendAPIResponse(msg); \
        }

    #define BEAM_API_HANDLE_FUNC(api, name, ...) \
         virtual void onHandle##api(const JsonRpcId& id, api&& data);

    #define BEAM_API_PARSE_FUNC(api, name, ...) \
        [[nodiscard]] std::pair<api, MethodInfo> onParse##api(const JsonRpcId& id, const json& msg);

    // BEAM_API_REG MUST BE SAFE TO CALL FROM ANY THREAD
    // Don't do anything with walletdb, providers &c.
    #define BEAM_API_REG_METHOD(api, name, writeAccess, isAsync, appsAllowed) \
        regMethod(name, {                                                     \
            [this] (const JsonRpcId &id, const json &msg) {                   \
                auto parseRes = onParse##api(id, msg);                        \
                onHandle##api(id, std::move(parseRes.first));                 \
            },                                                                \
            [this] (const JsonRpcId &id, const json &msg) -> MethodInfo {     \
                auto parseRes = onParse##api(id, msg);                        \
                return parseRes.second;                                       \
            },                                                                \
            writeAccess, isAsync, appsAllowed                                 \
        });

    class ApiBase
        : public IWalletApi
    {
    public:
        static inline const char JsonRpcHeader[] = "jsonrpc";
        static inline const char JsonRpcVersion[] = "2.0";

        // This is for jscript compatibility
        // Number.MAX_SAFE_INTEGER
        static inline const auto kMaxAllowedInt = AmountBig::Type(9'007'199'254'740'991U);

        // user api key and read/write access
        ApiBase(IWalletApiHandler& handler, const ApiInitData& initData);

        void sendError(const JsonRpcId& id, ApiError code, const std::string& data = "");

        boost::optional<ParseResult> parseAPIRequest(const char* data, size_t size) override;
        ApiSyncMode executeAPIRequest(const char *data, size_t size) override;
        std::string fromError(const std::string& request, ApiError code, const std::string& errorText) override;

        template<typename T>
        static void doPagination(size_t skip, size_t count, std::vector<T>& res)
        {
            if (count > 0)
            {
                size_t start = skip;
                size_t size = res.size();

                if (start < size)
                {
                    res.erase(res.begin(), res.begin() + start);
                    if (count < res.size())
                    {
                        res.erase(res.begin() + count, res.end());
                    }
                }
                else res = {};
            }
        }

        //
        // getMandatory....
        // return param if it exists and is of the requested type & constraints or throw otherwise
        //
        template<typename T>
        static T getMandatoryParam(const json &params, const std::string &name)
        {
            if (auto param = getOptionalParam<T>(params, name))
            {
                return *param;
            }
            throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, "Parameter '" + name + "' doesn't exist.");
        }

        //
        // getOptional... throws only if type constraints are violated
        //
        template<typename T>
        static boost::optional<T> getOptionalParam(const json& params, const std::string& name)
        {
            if(auto raw = getOptionalParam<const json&>(params, name))
            {
                if (type_check<T>(*raw))
                {
                    return type_get<T>(*raw);
                }

                auto tname = type_name<T>();
                throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, "Parameter '" + name + "' must be a " + tname + ".");
            }

            return boost::none;
        }

        static bool hasParam(const json &params, const std::string &name);

        bool isApp() const
        {
            return !_appId.empty();
        }

        const std::string& getAppId() const
        {
            return _appId;
        }

        const std::string& getAppName() const
        {
            return _appName;
        }

        void takeGuardPtr(WeakPtr ptr)
        {
            _weakSelf = ptr;
        }

    protected:
        struct Method
        {
            std::function<void(const JsonRpcId &id, const json &msg)> execFunc;
            std::function<IWalletApi::MethodInfo (const JsonRpcId &id, const json &msg)> parseFunc;

            bool writeAccess = true;
            bool isAsync     = true;
            bool appsAllowed = false;
        };

        void regMethod(const std::string& name, Method method)
        {
            _methods[name] = std::move(method);
        }

        IWalletApi::WeakPtr _weakSelf;
        IWalletApiHandler& _handler;

        boost::optional<ApiCallInfo> parseCallInfo(const char* data, size_t size);

    private:
        static json formError(const JsonRpcId& id, ApiError code, const std::string& data = "");

        template<typename TRes>
        boost::optional<TRes> callGuarded(const JsonRpcId& rpcid, std::function<TRes (void)> func)
        {
            try
            {
                return func();
            }
            catch (const nlohmann::detail::type_error& e)
            {
                auto error = formError(rpcid, ApiError::InvalidParamsJsonRpc, e.what());
                _handler.onParseError(error);
            }
            catch (const nlohmann::detail::exception& e)
            {
                auto error = formError(rpcid, ApiError::InvalidJsonRpc, e.what());
                _handler.onParseError(error);
            }
            catch (const jsonrpc_exception& e)
            {
                auto error = formError(rpcid, e.code(), e.whatstr());
                switch(e.code())
                {
                case ApiError::InvalidJsonRpc:
                case ApiError::InvalidParamsJsonRpc:
                    _handler.onParseError(error);
                    break;
                default:
                    _handler.sendAPIResponse(error);
                }
            }
            catch (const std::runtime_error& e)
            {
                auto error = formError(rpcid, ApiError::InternalErrorJsonRpc, e.what());
                _handler.sendAPIResponse(error);
            }
            catch (const std::exception& e)
            {
                auto error = formError(rpcid, ApiError::InternalErrorJsonRpc, e.what());
                _handler.sendAPIResponse(error);
            }
            catch (...)
            {
                auto error = formError(rpcid, ApiError::InternalErrorJsonRpc, "API call failed, please take a look at logs");
                _handler.sendAPIResponse(error);
            }

            return boost::none;
        }

    private:
        ApiACL _acl;
        std::string _appId;
        std::string _appName;
        std::unordered_map <std::string, Method> _methods;
    };

    // boost::optional<json> is not defined intentionally, use const json& instead
    template<>
    boost::optional<json> ApiBase::getOptionalParam<json>(const json &params, const std::string &name);

    template<>
    boost::optional<const json&> ApiBase::getOptionalParam<const json&>(const json &params, const std::string &name);
}
