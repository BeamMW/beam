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
#include "v7_1_api.h"
#include "version.h"

namespace beam::wallet
{
    std::pair<DeriveID, IWalletApi::MethodInfo> V71Api::onParseDeriveID(const JsonRpcId& id, const nlohmann::json& params)
    {
        DeriveID message;
        message.tag = getMandatoryParam<NonEmptyString>(params, "tag");
        return std::make_pair(std::move(message), MethodInfo());
    }

    void V71Api::getResponse(const JsonRpcId& id, const DeriveID::Response& res, json& msg)
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
}
