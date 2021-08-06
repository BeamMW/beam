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

#include "model/app_model.h"
#include "webapi_shaders.h"
#include "wallet/api/i_wallet_api.h"

namespace beamui::applications
{
    class WebAPI_Beam
        : public QObject
        , public beam::wallet::IWalletApiHandler
        , public std::enable_shared_from_this<WebAPI_Beam>
    {
        struct ApiHandlerProxy: beam::wallet::IWalletApiHandler
        {
            void sendAPIResponse(const beam::wallet::json& result) override
            {
                if (auto handler = _handler.lock())
                {
                    handler->sendAPIResponse(result);
                }
            }
            std::weak_ptr<beam::wallet::IWalletApiHandler> _handler;
        };

        Q_OBJECT
        Q_PROPERTY(QMap<QString, QVariant> assets READ getAssets NOTIFY assetsChanged)

    public:
        // Do not call directly, use ::Create instead
        WebAPI_Beam(const std::string& version, const std::string& appid, const std::string& appname);
        ~WebAPI_Beam() override;

        static std::shared_ptr<WebAPI_Beam> Create(const std::string& version, const std::string& appid, const std::string& appname);

        [[nodiscard]] QMap<QString, QVariant> getAssets();
        Q_INVOKABLE void sendApproved(const QString& request);
        Q_INVOKABLE void sendRejected(const QString& request);
        Q_INVOKABLE void contractInfoApproved(const QString& request);
        Q_INVOKABLE void contractInfoRejected(const QString& request);

    public slots:
       int test();
       void callWalletApi(const QString& request);

    signals:
       void callWalletApiResult(const QString& result);
       void assetsChanged();
       void approveSend(const QString& request, const QMap<QString, QVariant>& info);
       void approveContractInfo(const QString& request, const QMap<QString, QVariant>& info, QList<QMap<QString, QVariant>> amounts);

    public:
        [[nodiscard]] std::string getAppId() const
        {
            return _appId;
        }

        [[nodiscard]] std::string getAppName() const
        {
            return _appName;
        }

    private:
        void AnyThread_getSendConsent(const std::string& request, const beam::wallet::IWalletApi::ParseResult&);
        void AnyThread_getContractInfoConsent(const std::string &request, const beam::wallet::IWalletApi::ParseResult &);

    private:
        void UIThread_callWalletApiImp(const std::string& request);

        // This can be called from any thread
        void AnyThread_sendError(const std::string& request, beam::wallet::ApiError err, const std::string& message);

        // This can be called from any thread
        void AnyThread_sendAPIResponse(const beam::wallet::json& result);

        // This is called from API (REACTOR) thread
        void sendAPIResponse(const beam::wallet::json& result) override;

        // API should be accessed only in context of the reactor thread
        using ApiPtr = beam::wallet::IWalletApi::Ptr;
        ApiPtr _walletAPI;
        std::shared_ptr<ApiHandlerProxy> _walletAPIProxy;

        std::string _appId;
        std::string _appName;

        AssetsManager::Ptr _amgr;
        std::set<beam::Asset::ID> _mappedAssets;
    };
}
