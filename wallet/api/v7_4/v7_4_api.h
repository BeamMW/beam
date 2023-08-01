// Copyright 2023 The Beam Team
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
#include "v7_4_api_defs.h"
#include "wallet/api/v7_3/v7_3_api.h"

namespace beam::wallet
{
class V74Api: public V73Api
{
public:
    // CTOR MUST BE SAFE TO CALL FROM ANY THREAD
    V74Api(IWalletApiHandler& handler, unsigned long avMajor, unsigned long avMinor, const ApiInitData& init);
    ~V74Api() override = default;

    V7_4_API_METHODS(BEAM_API_PARSE_FUNC)
    V7_4_API_METHODS(BEAM_API_RESPONSE_FUNC)
    V7_4_API_METHODS(BEAM_API_HANDLE_FUNC)
};
}
