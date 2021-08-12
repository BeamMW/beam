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

//#include "model/app_model.h"
//#include "webapi_shaders.h"
#include "wallet/api/i_wallet_api.h"
#include "wallet/core/wallet_db.h"
#include "wallet/client/wallet_model_async.h"

#include <boost/any.hpp>

namespace beam::wallet
{
    class WalletClient;
}

namespace beam::applications
{
    using namespace beam::wallet;
    using WalletClientPtr = std::shared_ptr<beam::wallet::WalletClient>;
    using WalletClientWeakPtr = std::weak_ptr<beam::wallet::WalletClient>;
    using ApproveMap = std::map<std::string, boost::any>;
    using ApproveAmounts = std::vector<std::map<std::string, boost::any>>;

    class WebAPI_Beam
        : public beam::wallet::IWalletApiHandler
        , public std::enable_shared_from_this<WebAPI_Beam>
    {
    protected:
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

    public:

        using Ptr       = std::shared_ptr<WebAPI_Beam>;
        using WeakPtr   = std::weak_ptr<WebAPI_Beam>;

        // Do not call directly, use ::Create instead
        WebAPI_Beam(WalletClientPtr wc, IWalletDB::Ptr db, const std::string& version, const std::string& appid, const std::string& appname);
        ~WebAPI_Beam() override;

        //static std::shared_ptr<WebAPI_Beam> Create(const std::string& version, const std::string& appid, const std::string& appname);

        //[[nodiscard]] QMap<QString, QVariant> getAssets();
        void sendApproved(const std::string& request);
        void sendRejected(const std::string& request);
        void contractInfoApproved(const std::string& request);
        void contractInfoRejected(const std::string& request);

       void callWalletApi(const std::string& request);

    //signals:
       virtual void onCallWalletApiResult(const std::string& result) {}
    //   void assetsChanged();
       virtual void onApproveSend(const std::string& request, const ApproveMap& info) {}
       virtual void onApproveContractInfo(const std::string& request, const ApproveMap& info, const ApproveAmounts& amounts) {}

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

        IWalletModelAsync& getAsyncWallet();

        // API should be accessed only in context of the reactor thread
        using ApiPtr = beam::wallet::IWalletApi::Ptr;
        ApiPtr _walletAPI;
    protected:
        std::shared_ptr<ApiHandlerProxy> _walletAPIProxy;
    private:
        std::string _appId;
        std::string _appName;

        //AssetsManager::Ptr _amgr;
        std::set<beam::Asset::ID> _mappedAssets;
        WalletClientPtr _client;
    };
}
