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

#include "core/block_crypt.h"
#include "utility/serialize_fwd.h"
#include <string>

namespace beam::wallet
{
    struct VerificationInfo
    {
        beam::Asset::ID m_assetID;
        bool m_verified;
        std::string m_icon;
        Timestamp m_updateTime = 0;
        std::string m_color;
        SERIALIZE(m_assetID, m_verified, m_icon, m_color, m_updateTime);
    };
}
