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
#include "v7_0/v7_0_api.h"
#include "v7_1/v7_1_api.h"
#include "v7_2/v7_2_api.h"
#include "v7_3/v7_3_api.h"

namespace beam::wallet
{
    namespace
    {
        uint32_t SApiVer2NApiVer(std::string sver)
        {
            if (sver == kApiVerCurrent)
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

            switch (version)
            {
#define MACRO(major, minor) case ApiVer##major##_##minor:
                ApiVersions(MACRO)
#undef MACRO
                 return true;
            default:
                return false;
            }
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
            case ApiVer7_3:
            {
                auto api = new V73Api(handler, 7, 3, data);
                auto ptr = IWalletApi::Ptr(api);
                api->takeGuardPtr(ptr);
                return ptr;
            }
            case ApiVer7_2:
            {
                auto api = new V72Api(handler, 7, 2, data);
                auto ptr = IWalletApi::Ptr(api);
                api->takeGuardPtr(ptr);
                return ptr;
            }

            case ApiVer7_1:
            {
                auto api = new V71Api(handler, 7, 1, data);
                auto ptr = IWalletApi::Ptr(api);
                api->takeGuardPtr(ptr);
                return ptr;
            }

            case ApiVer7_0:
            {
                auto api = new V70Api(handler, 7, 0, data);
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
