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

#include "version_info.h"
#include "utility/string_helpers.h"

namespace beam::wallet
{
    std::string VersionInfo::to_string(VersionInfo::Application a)
    {
        switch (a)
        {
            case Application::DesktopWallet:
                return std::string(desktopAppStr);
            case Application::AndroidWallet:
                return std::string(androidAppStr);
            case Application::IOSWallet:
                return std::string(iosAppStr);
            default:
                return std::string(unknownAppStr);
        }
    }

    VersionInfo::Application VersionInfo::from_string(const std::string& type)
    {
        if (type == desktopAppStr)
            return Application::DesktopWallet;
        else if (type == androidAppStr)
            return Application::AndroidWallet;
        else if (type == iosAppStr)
            return Application::IOSWallet;
        else return Application::Unknown;
    }

    bool VersionInfo::operator==(const VersionInfo& other) const
    {
        return m_application == other.m_application
            && m_version == other.m_version;
    }

    bool VersionInfo::operator!=(const VersionInfo& other) const
    {
        return !(*this == other);
    }

    Version WalletImplVerInfo::getBeamCoreVersion() const
    {
        return m_version;
    }

    uint32_t WalletImplVerInfo::getUIrevision() const
    {
        return m_UIrevision;
    }

    bool WalletImplVerInfo::operator==(const WalletImplVerInfo& other) const
    {
        return m_application == other.m_application
            && m_version == other.m_version
            && m_UIrevision == other.m_UIrevision
            && m_title == other.m_title
            && m_message == other.m_message;
    }

    bool WalletImplVerInfo::operator!=(const WalletImplVerInfo& other) const
    {
        return !(*this == other);
    }
    
} // namespace beam::wallet
