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
#include "i_wallet_api.h"
#include "v6_0/wallet_api.h"
#include <stdexcept>

namespace beam::wallet
{
    namespace
    {
        const std::string kVerCurrent = "current";

        uint32_t SApiVer2NApiVer(std::string sver)
        {
            if (sver.empty() || sver == kVerCurrent)
            {
                return ApiVerCurrent;
            }

            sver.erase(std::remove(sver.begin(), sver.end(), '.'), sver.end());
            return std::stoul(sver);
        }
    }

    bool IWalletApi::ValidateAPIVersion(const std::string& sver)
    {
        try
        {
            const auto version = SApiVer2NApiVer(sver);
            return ApiVerMin >= version && ApiVerMax <= version;
        }
        catch(std::exception& ex)
        {
            LOG_WARNING() << "ValidateAPIVersion: " << ex.what();
            return false;
        }
    }

    IWalletApi::Ptr IWalletApi::CreateInstance(const std::string& sversion, IWalletApiHandler& handler, const InitData& data)
    {
        const auto version = SApiVer2NApiVer(sversion);
        return IWalletApi::CreateInstance(version, handler, data);
    }

    IWalletApi::Ptr IWalletApi::CreateInstance(uint32_t version, IWalletApiHandler& handler, const InitData& data)
    {
        switch (version)
        {
        case ApiVer6_0:
            return std::make_shared<WalletApi>(handler, data.acl, data.appid, data.walletDB, data.wallet, data.swaps, data.contracts);
        default:
            return nullptr;
        }
    }
}
