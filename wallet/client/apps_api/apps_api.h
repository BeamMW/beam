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
#pragma once

#include <string>

namespace beam::wallet
{
    class AppsApi
    {
    public:
        AppsApi(std::string appid, std::string appname);
        ~AppsApi();

        [[nodiscard]] std::string getAppId() const
        {
            return _appId;
        }

        [[nodiscard]] std::string getAppName() const
        {
            return _appName;
        }

    private:
        std::string _appId;
        std::string _appName;
    };
}
