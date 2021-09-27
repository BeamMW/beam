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
#include "apps_utils.h"
#include "core/ecc_native.h"

namespace beam::wallet
{
    namespace
    {
        const char* kAppIDPrefix = "appid:";
    }

    std::string GenerateAppID(const std::string& appName, const std::string& appUrl)
    {
        ECC::Hash::Value hv;
        ECC::Hash::Processor() << appName << appUrl >> hv;
        auto appid = kAppIDPrefix + hv.str();
        return appid;
    }

    std::string StripAppIDPrefix(const std::string& appId)
    {
        auto res = appId;

        size_t pos = appId.find(kAppIDPrefix);
        if (pos != std::string::npos)
        {
            std::string prefix(kAppIDPrefix);
            res.erase(pos, prefix.length());
        }

        return res;
    }
}