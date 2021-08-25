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

#include "wallet/core/version.h"
#include "utility/serialize_fwd.h"
#include "stdint.h"
#include <string>

namespace beam::wallet
{
    constexpr std::string_view desktopAppStr =  "desktop";
    constexpr std::string_view androidAppStr =  "android";
    constexpr std::string_view iosAppStr =      "ios";
    constexpr std::string_view unknownAppStr =  "unknown";

    struct VersionInfo
    {
        enum class Application : uint32_t
        {
            DesktopWallet,
            AndroidWallet,
            IOSWallet,
            Unknown
        };

        Application m_application = Application::Unknown;
        beam::Version m_version;

        SERIALIZE(m_application, m_version);

        static std::string to_string(Application);
        static Application from_string(const std::string&);

        bool operator==(const VersionInfo& other) const;
        bool operator!=(const VersionInfo& other) const;
    };

    struct WalletImplVerInfo
    {
        VersionInfo::Application m_application = VersionInfo::Application::DesktopWallet;
        Version m_version;          // Beam Core Wallet library version info
        std::string m_title;        // RFU
        std::string m_message;      // RFU
        uint32_t m_UIrevision = 0;  // UI revision

        Version getBeamCoreVersion() const;
        uint32_t getUIrevision() const;

        SERIALIZE(m_application, m_version, m_title, m_message, m_UIrevision);

        bool operator==(const WalletImplVerInfo& other) const;
        bool operator!=(const WalletImplVerInfo& other) const;
    };

} // namespace beam::wallet
