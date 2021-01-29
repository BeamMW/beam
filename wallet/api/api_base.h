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

#include <boost/optional.hpp>
#include <boost/serialization/strong_typedef.hpp>
#include "api_errors.h"
#include "wallet/core/common.h"
#include "utility/common.h"

namespace beam::wallet
{
    struct IApiBaseHandler {
        virtual void onRPCError(const json& msg) = 0;
        virtual ~IApiBaseHandler() = default;
    };

    class ApiBase
    {
    public:
        static inline const char JsonRpcHeader[] = "jsonrpc";
        static inline const char JsonRpcVersion[] = "2.0";

        // user api key and read/write access
        using ACL = boost::optional <std::map<std::string, bool>>;
        using Ptr = std::shared_ptr<ApiBase>;

        explicit ApiBase(IApiBaseHandler &handler, ACL acl = boost::none);

        //
        // parse and execute request
        //
        bool parseJSON(const char *data, size_t size);

        //
        // getMandatory....
        // return param if it exists and is of the requested type & constraints or throw otherwise
        //
        template<typename T>
        static T getMandatoryParam(const json &params, const std::string &name, const JsonRpcId& id)
        {
            auto raw = getMandatoryParam<const json&>(params, name, id);
            return raw.get<T>();
        }

        static const char *getErrorMessage(ApiError code);
        static bool existsJsonParam(const json &params, const std::string &name);
        static void throwParameterAbsence(const JsonRpcId &id, const std::string &name);

        class ParameterReader
        {
        public:
            ParameterReader(const JsonRpcId &id, const json &params);
            Amount readAmount(const std::string &name, bool isMandatory = true, Amount defaultValue = 0);
            boost::optional <TxID> readTxId(const std::string &name = "txId", bool isMandatory = true);
        private:
            const JsonRpcId &m_id;
            const json &m_params;
        };

    protected:
        struct Method
        {
            std::function<void(const JsonRpcId &id, const json &msg)> func;
            bool writeAccess;
        };

        std::unordered_map <std::string, Method> _methods;
        IApiBaseHandler& _handler;
        ACL _acl;
    };

    template<>
    json ApiBase::getMandatoryParam<json>(const json &params, const std::string &name, const JsonRpcId &id);

    template<>
    const json& ApiBase::getMandatoryParam<const json&>(const json &params, const std::string &name, const JsonRpcId &id);

    template<>
    std::string ApiBase::getMandatoryParam<std::string>(const json &params, const std::string &name, const JsonRpcId &id);

    template<>
    bool ApiBase::getMandatoryParam<bool>(const json &params, const std::string &name, const JsonRpcId &id);

    BOOST_STRONG_TYPEDEF(json, JsonArray)
    template<>
    JsonArray ApiBase::getMandatoryParam<JsonArray>(const json &params, const std::string &name, const JsonRpcId &id);

    BOOST_STRONG_TYPEDEF(unsigned int, PositiveUnit64)
    template<>
    PositiveUnit64 ApiBase::getMandatoryParam<PositiveUnit64>(const json &params, const std::string &name, const JsonRpcId &id);

    template<>
    uint32_t ApiBase::getMandatoryParam<uint32_t>(const json &params, const std::string &name, const JsonRpcId &id);
}
