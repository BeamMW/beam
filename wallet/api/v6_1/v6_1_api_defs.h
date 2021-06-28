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

namespace beam::wallet
{
    #define V6_1_API_METHODS(macro) \
        macro(EvSubscribe,    "ev_subscribe",    API_READ_ACCESS, API_SYNC,  APPS_ALLOWED) \
        macro(EvUnsubscribe,  "ev_unsubscribe",  API_READ_ACCESS, API_SYNC,  APPS_ALLOWED) \
        macro(GetVersion,     "get_version",     API_READ_ACCESS, API_SYNC,  APPS_ALLOWED)

    struct EvSubscribe
    {
        struct Response
        {
            bool result;
        };
    };

    struct EvUnsubscribe
    {
        struct Response
        {
            bool result;
        };
    };

    struct GetVersion
    {
        struct Response
        {
            std::string apiVersion;
            unsigned long apiVersionMinor;
            unsigned long apiVersionMajor;

            std::string beamVersion;
            unsigned long beamVersionMajor;
            unsigned long beamVersionMinor;
            unsigned long beamVersionRevision;

            std::string beamCommitHash;
            std::string beamBranchName;
        };
    };
}
