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

#include "wallet/api/i_wallet_api.h"
#include "wallet/core/contracts/i_shaders_manager.h"
#include "wallet/client/apps_api/apps_api.h"


class AppsApiUI : public beam::wallet::AppsApi<AppsApiUI>
{

public:
    AppsApiUI(const std::string& appid, const std::string& appname);
    ~AppsApiUI() override = default;
  
    int test();
    void callWalletApi(const std::string& request);
    void sendApproved(const std::string& request);
    void sendRejected(const std::string& request);
    void contractInfoApproved(const std::string& request);
    void contractInfoRejected(const std::string& request);
    
private:
    
    friend class beam::wallet::AppsApi<AppsApiUI>;
    
    void AnyThread_sendApiResponse(std::string&& result) override;
    void ClientThread_getSendConsent(const std::string& request, const nlohmann::json& info, const nlohmann::json& amounts) override;
    void ClientThread_getContractConsent(const std::string& request, const nlohmann::json& info, const nlohmann::json& amounts) override;
    
private:    
    std::string prepareInfo4QT(const nlohmann::json& info);
    std::string prepareAmounts4QT(const nlohmann::json& amounts);
    std::string AmountToUIString(const beam::Amount& value);
};


