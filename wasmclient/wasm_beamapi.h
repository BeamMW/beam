// Copyright 2018-2021 The Beam Team
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

#include "wallet/client/apps_api/apps_api.h"
#include <emscripten/val.h>

class WasmAppApi
    : public beam::wallet::AppsApi<WasmAppApi>
    , public std::enable_shared_from_this<WasmAppApi>
{
public:
    WasmAppApi(const std::string appid, const std::string appname);
    ~WasmAppApi() override = default;

    // This is visible to jscript
    void CallWalletAPI(const std::string& request);

    // This is visible to jscript
    void SetResultHandler(emscripten::val handler);

    typedef std::function<void (const std::string&, const std::string&, const std::string&)> ClientThread_ContractConsentHandler;
    typedef std::function<void (const std::string&, const std::string&)> ClientThread_SendConsentHandler;
    typedef std::function<void (std::function<void (void)>&&)> AnyThread_PostHandler;

    void SetContractConsentHandler(ClientThread_ContractConsentHandler handler);
    void SetSendConsentHandler(ClientThread_SendConsentHandler handler);
    void SetPostToClientHandler(AnyThread_PostHandler&& handler);

    // AppsApi overrides
    void AnyThread_sendApiResponse(std::string&& result) override;
    void ClientThread_getSendConsent(const std::string& request, const nlohmann::json& info, const nlohmann::json& amounts) override;
    void ClientThread_getContractConsent(const std::string& request, const nlohmann::json& info, const nlohmann::json& amounts) override;

private:
    std::unique_ptr<emscripten::val> m_jsResultReceiver;
    ClientThread_ContractConsentHandler m_ctContractConsent;
    ClientThread_SendConsentHandler m_ctSendConsent;
    AnyThread_PostHandler m_postToClient;
};
