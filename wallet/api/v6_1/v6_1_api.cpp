// Copyright 2020 The Beam Team
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
    V61Api::V61Api(IWalletApiHandler& handler, unsigned long avMajor, unsigned long avMinor, const ApiInitData& init)
        : V6Api(handler, init)
        , _apiVersionMajor(avMajor)
        , _apiVersionMinor(avMinor)
    {
        std::stringstream ss;
        ss << avMajor << "." << avMinor;
        _apiVersion = ss.str();

        _wallet = init.wallet;
        if(_wallet)
        {
            _wallet->Subscribe(this);
        }

        V6_1_API_METHODS(BEAM_API_REG_METHOD)
    }

    V61Api::~V61Api()
    {
        if (_wallet)
        {
            _wallet->Unsubscribe(this);
        }
    }
}
