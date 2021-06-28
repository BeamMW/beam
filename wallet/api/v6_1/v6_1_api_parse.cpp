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
#include "v6_1_api.h"

namespace beam::wallet
{
    std::pair<EvSubscribe, IWalletApi::MethodInfo> V61Api::onParseEvSubscribe(const JsonRpcId& id, const nlohmann::json& params)
    {
        EvSubscribe message{};
        return std::make_pair(message, MethodInfo());
    }

    void V61Api::getResponse(const JsonRpcId& id, const EvSubscribe::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", res.result}
        };
    }

    std::pair<EvUnsubscribe, IWalletApi::MethodInfo> V61Api::onParseEvUnsubscribe(const JsonRpcId& id, const nlohmann::json& params)
    {
        EvUnsubscribe message{};
        return std::make_pair(message, MethodInfo());
    }

    void V61Api::getResponse(const JsonRpcId& id, const EvUnsubscribe::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", res.result}
        };
    }

    std::pair<GetVersion, IWalletApi::MethodInfo> V61Api::onParseGetVersion(const JsonRpcId& id, const nlohmann::json& params)
    {
        GetVersion message{};
        return std::make_pair(message, MethodInfo());
    }

    void V61Api::getResponse(const JsonRpcId& id, const GetVersion::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result",
                {
                    {"api_version",        res.apiVersion},
                    {"api_version_major",  res.apiVersionMajor},
                    {"api_version_minor",  res.apiVersionMinor},
                    {"beam_version",       res.beamVersion},
                    {"beam_version_major", res.beamVersionMajor},
                    {"beam_version_minor", res.beamVersionMinor},
                    {"beam_version_rev",   res.beamVersionRevision},
                    {"beam_commit_hash",   res.beamCommitHash},
                    {"beam_branch_name",   res.beamBranchName}
                }
            }
        };
    }
}
