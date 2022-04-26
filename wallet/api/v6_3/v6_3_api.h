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
#include "v6_3_api_defs.h"
#include "wallet/api/v6_1/v6_1_api.h"

namespace beam::wallet
{
    class V63Api: public V61Api
    {
    public:
        // CTOR MUST BE SAFE TO CALL FROM ANY THREAD
        V63Api(IWalletApiHandler& handler, unsigned long avMajor, unsigned long avMinor, const ApiInitData& init);
        ~V63Api() override = default;

        V6_3_API_METHODS(BEAM_API_PARSE_FUNC)
        V6_3_API_METHODS(BEAM_API_RESPONSE_FUNC)
        V6_3_API_METHODS(BEAM_API_HANDLE_FUNC)
    private:
#ifdef BEAM_ETH_API_EMULATION
        void FillBlockResponse(const JsonRpcId& id, const BlockDetails::Response& res, const std::vector<std::string>& txHashes, json& msg) const;
        std::vector<std::string> GetTxByHeight(Height h) const;
#endif
    };
}
