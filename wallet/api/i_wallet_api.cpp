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
#include <stdexcept>
#include "i_wallet_api.h"
#include "v6_0/v6_api.h"
#include "v6_1/v6_1_api.h"
#include "v6_3/v6_3_api.h"

namespace beam::wallet
{
    namespace
    {
        const std::string kVerCurrent = "current";

        uint32_t SApiVer2NApiVer(std::string sver)
        {
            if (sver == kVerCurrent)
            {
                return ApiVerCurrent;
            }

            sver.erase(std::remove(sver.begin(), sver.end(), '.'), sver.end());
            return std::stoul(sver);
        }
    }

    bool IWalletApi::ValidateAPIVersion(const std::string& sver)
    {
        if (sver.empty())
        {
            return false;
        }

        try
        {
            const auto version = SApiVer2NApiVer(sver);
            return version >= ApiVerMin && version <= ApiVerMax;
        }
        catch(std::exception& ex)
        {
            LOG_WARNING() << "ValidateAPIVersion: " << ex.what();
            return false;
        }
    }

    IWalletApi::Ptr IWalletApi::CreateInstance(const std::string& sversion, IWalletApiHandler& handler, const ApiInitData& data)
    {
        // MUST BE SAFE TO CALL FROM ANY THREAD
        const auto version = SApiVer2NApiVer(sversion);
        return IWalletApi::CreateInstance(version, handler, data);
    }

    IWalletApi::Ptr IWalletApi::CreateInstance(uint32_t version, IWalletApiHandler& handler, const ApiInitData& data)
    {
        // MUST BE SAFE TO CALL FROM ANY THREAD
        switch (version)
        {
        case ApiVer6_3:
            {
                auto api = new V63Api(handler, 6, 3, data);
                auto ptr = IWalletApi::Ptr(api);
                api->takeGuardPtr(ptr);
                return ptr;
            }

        case ApiVer6_2:
            {
                // 6.2 Api is the same as 6.1
                auto api = new V61Api(handler, 6, 2, data);
                auto ptr = IWalletApi::Ptr(api);
                api->takeGuardPtr(ptr);
                return ptr;
            }

        case ApiVer6_1:
            {
                auto api = new V61Api(handler, 6, 1, data);
                auto ptr = IWalletApi::Ptr(api);
                api->takeGuardPtr(ptr);
                return ptr;
            }

        case ApiVer6_0:
            {
                auto api = new V6Api(handler, data);
                auto ptr = IWalletApi::Ptr(api);
                api->takeGuardPtr(ptr);
                return ptr;
            }

        default:
            return nullptr;
        }
    }
}
