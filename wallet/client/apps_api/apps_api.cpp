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
#include "apps_api.h"

namespace beam::wallet
{
    void printMap(const std::string& prefix, const json& info)
    {
        for (const auto& pair: info.items())
        {
            LOG_INFO () << prefix << pair.key() << " = " << pair.value();
        }
    }

    void printApproveLog(const std::string& preamble, const std::string& appid, const std::string& appname, const json& info, const json& amounts)
    {
        LOG_INFO() << preamble << " (" << appname << ", " << appid << "):";
        printMap("\t", info);

        if (!amounts.empty())
        {
            for (const auto &amountMap : amounts)
            {
                LOG_INFO() << "\tamount entry:";
                printMap("\t\t", amountMap);
            }
        }
    }
}
