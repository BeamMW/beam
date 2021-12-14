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
#include "web_api_creator.h"
#include "wallet/api/i_wallet_api.h"
#include "wallet/core/common.h"
#include "wallet/client/apps_api/apps_utils.h"
#include "bvm/invoke_data.h"

WebAPICreator::WebAPICreator()
{
}

void WebAPICreator::createApi(WalletModel::Ptr walletModel, const std::string& verWant, const std::string& verMin, const std::string &appName, const std::string &appUrl)
{    
    std::string version;
    if (beam::wallet::IWalletApi::ValidateAPIVersion(verWant))
    {
        version = verWant;
    }
    
    else if (beam::wallet::IWalletApi::ValidateAPIVersion(verMin))
    {
        version = verMin;
    }
    
    const auto appid = beam::wallet::GenerateAppID(appName, appUrl);
    auto guard = this;
    
    AppsApiUI::ClientThread_Create(walletModel.get(), version, appid, appName,
                                   [this, guard, version, appName, appid] (AppsApiUI::Ptr api) {
        if (guard)
        {
            _api = std::move(api);
            LOG_INFO() << "API created: " << version << ", " << appName << ", " << appid;
        }
        else
        {
            LOG_INFO() << "WebAPICreator destroyed before api created:" << version << ", " << appName << ", " << appid;
        }
    });
}

bool WebAPICreator::apiSupported(const std::string& apiVersion) const
{
    return beam::wallet::IWalletApi::ValidateAPIVersion(apiVersion);
}

std::string WebAPICreator::generateAppID(const std::string& appName, const std::string& appUrl)
{
    const auto appid = beam::wallet::GenerateAppID(appName, appUrl);
    return appid;
}
