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

#include "wallet/api/i_wallet_api.h"
#include "wallet/client/wallet_client.h"

namespace beam::wallet
{
    void printApproveLog(const std::string& preamble,
                         const std::string& appid,
                         const std::string& appname,
                         const nlohmann::json& info,
                         const nlohmann::json& amounts);

    template<typename Target>
    class AppsApi
        : public beam::wallet::IWalletApiHandler
    {
        // this is to access private constructor in Target
        struct Allocator: std::allocator<Target>
        {
            void construct(void* p,
                           std::string appid,
                           std::string appname)
            {
                ::new(p) Target(std::move(appid), std::move(appname));
            }

            void destroy(Target* p)
            {
                p->~Target();
            }
        };

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

    protected:
        AppsApi(std::string appid, std::string appname)
            : _appId(std::move(appid))
            , _appName(std::move(appname))
        {
            _walletAPIProxy = std::make_shared<ApiHandlerProxy>();
            LOG_INFO () << "AppsApi created for " << _appName << ", " << _appId;
        }

        virtual ~AppsApi()
        {
            _client->getAsync()->makeIWTCall(
                [proxy = std::move(_walletAPIProxy), api = std::move(_walletAPI)] () mutable -> boost::any {
                    // api should be destroyed in context of the wallet thread
                    // it is ASSUMED to be the last call in api calls chain
                    api.reset();
                    proxy.reset();
                    return boost::none;
                    },
                [] (const boost::any&){
                }
            );
            LOG_INFO () << "AppsApi destroyed for " << _appName << ", " << _appId;
        }

    public:
        typedef std::shared_ptr<Target> Ptr;
        typedef std::weak_ptr<Target> WeakPtr;

        static void UIThread_Create(WalletClient* client,
                       std::string version,
                       std::string appid,
                       std::string appname,
                       std::function<void (Ptr)> cback)
        {
            client->getAsync()->makeIWTCall(
                [client, appid, appname]() -> boost::any {
                    //
                    // THIS IS REACTOR THREAD
                    //
                    return client->IWThread_createAppShaders(appid, appname);
                },
                [client, cback, version, appid, appname](boost::any aptr) {
                    //
                    // THIS IS UI THREAD
                    //
                    auto wapi = std::allocate_shared<Target>(Allocator(), appid, appname);
                    wapi->_walletAPIProxy->_handler = wapi;
                    wapi->_client = client;
                    wapi->_weakSelf = wapi;

                    ApiInitData data;
                    auto shaders   = boost::any_cast<IShadersManager::Ptr>(aptr);
                    data.contracts = std::move(shaders);
                    data.swaps     = nullptr;
                    data.wallet    = client->getWallet();
                    data.walletDB  = client->getWalletDB();
                    data.appId     = appid;
                    data.appName   = appname;

                    wapi->_walletAPI = IWalletApi::CreateInstance(version, *wapi->_walletAPIProxy, data);
                    cback(std::move(wapi));
                }
            );
        }

        [[nodiscard]] std::string getAppId() const
        {
            return _appId;
        }

        [[nodiscard]] std::string getAppName() const
        {
            return _appName;
        }

        void AnyThread_callWalletApiDirectly(const std::string& request)
        {
            LOG_INFO () << "AppsApi direct call for " << getAppName() << ", " << getAppId() << "): " << request;

            //
            // Do not assume thread here
            // Should be safe to call from any thread
            //
            _client->getAsync()->makeIWTCall(
                [wp = _weakSelf, this, request]() -> boost::any {
                    auto locked = wp.lock();
                    if (!locked)
                    {
                        // this means that api is disconnected and destroyed already
                        // well, okay, nothing to do then
                        return boost::none;
                    }

                    _walletAPI->executeAPIRequest(request.c_str(), request.size());
                    return boost::none;
                },
                [] (const boost::any&) {
                }
            );
        }

        void AnyThread_callWalletApiChecked(const std::string& request)
        {
            struct CheckInfo {
                bool send;
                IWalletApi::ParseResult data;
            };

            LOG_INFO () << "AppsApi checkekd call for " << getAppName() << ", " << getAppId() << "): " << request;
            _client->getAsync()->makeIWTCall(
                [wp = _weakSelf, this, request]() -> boost::any {
                    auto locked = wp.lock();
                    if (!locked)
                    {
                        // this means that api is disconnected and destroyed already, this is normal
                        return boost::any();
                    }

                    if (auto pres = _walletAPI->parseAPIRequest(request.c_str(), request.size()); pres)
                    {
                        const auto& acinfo = pres->acinfo;
                        if (acinfo.appsAllowed)
                        {
                            if (acinfo.method == "tx_send")
                            {
                                CheckInfo info{true, *pres};
                                return info;
                            }

                            if (acinfo.method == "process_invoke_data")
                            {
                                CheckInfo info{false, *pres};
                                return info;
                            }

                            if (pres->minfo.fee > 0 || !pres->minfo.spend.empty())
                            {
                                LOG_INFO() << "Application called method " << acinfo.method << " that spends funds, but user consent is not handled";
                                assert(false);

                                AnyThread_sendApiError(request, ApiError::NotAllowedError, std::string());
                                return boost::any();
                            }

                            _walletAPI->executeAPIRequest(request.c_str(), request.size());
                            return boost::any();
                        }

                        LOG_INFO() << "Application requested call of the not allowed method: " << pres->acinfo.method;
                        AnyThread_sendApiError(request, ApiError::NotAllowedError, std::string());
                        return boost::any();
                    }
                    else
                    {
                        // parse failed, just log error and return. Error response is already sent back
                        LOG_ERROR() << "WebAPP API parse failed: " << request;
                        return boost::any();
                    }
                },
                [wp = _weakSelf, this, request] (const boost::any& any) {
                    if (any.empty())
                    {
                        return;
                    }

                    auto locked = wp.lock();
                    if (!locked)
                    {
                        // this means that api is disconnected and destroyed already, this is normal
                        return;
                    }

                    auto cr = boost::any_cast<CheckInfo>(any);
                    if (cr.send)
                    {
                        ClientThread_prepareSendConsent(request, cr.data);
                    }
                    else
                    {
                        ClientThread_prepareContractConsent(request, cr.data);
                    }
                }
            );
        }

        virtual void AnyThread_sendApiResponse(const beam::wallet::json& result)
        {
            auto str = result.dump();
            LOG_INFO() << "AnyThread_sendAPIResponse: " << str;
            if (!str.empty())
            {
                AnyThread_sendApiResponse(str);
            }
        }

        void AnyThread_sendApiError(const std::string& request, beam::wallet::ApiError err, const std::string& message)
        {
            //
            // Do not assume thread here
            // Should be safe to call from any thread
            //
            const auto error = _walletAPI->fromError(request, err, message);
            AnyThread_sendApiResponse(error);
        }

        virtual void AnyThread_sendApiResponse(const std::string& result) = 0;
        virtual void ClientThread_getSendConsent(const std::string& request, const nlohmann::json& info, const nlohmann::json& amounts) = 0;
        virtual void ClientThread_getContractConsent(const std::string& request, const nlohmann::json& info, const nlohmann::json& amounts) = 0;

    private:
        std::string extractComment(IWalletApi::ParseResult& parse)
        {
            if(parse.minfo.confirm_comment)
            {
                return *parse.minfo.confirm_comment;
            }

            if (parse.minfo.comment)
            {
                return *parse.minfo.comment;
            }

            return std::string();
        }

        void ClientThread_prepareContractConsent(const std::string& request, IWalletApi::ParseResult& parse)
        {
            json info = {
                {"comment", extractComment(parse)},
                {"fee",     parse.minfo.fee},
                {"isSpend", !parse.minfo.spend.empty()}
            };

            json amounts = json::array();
            bool isEnough = true;

            for(const auto& sinfo: parse.minfo.spend)
            {
                const auto assetId = sinfo.first;
                const auto amount  = sinfo.second;

                if (beam::AmountBig::get_Hi(amount) != 0)
                {
                    assert(false); // this should never happen but just in case
                    return AnyThread_sendApiError(request, ApiError::NotAllowedError, "amount is too big");
                }

                amounts.push_back({
                    {"amount",  beam::AmountBig::get_Lo(amount)},
                    {"assetID", assetId},
                    {"spend",   true}
                });

                beam::AmountBig::Type totalAmount = amount;
                if (assetId == beam::Asset::s_BeamID)
                {
                    totalAmount += beam::AmountBig::Type(parse.minfo.fee);
                }
                isEnough = isEnough && (totalAmount <= _client->getAvailable(assetId));
            }

            for(const auto& sinfo: parse.minfo.receive)
            {
                const auto amount  = sinfo.second;
                if (beam::AmountBig::get_Hi(amount) != 0)
                {
                    assert("!amount is too big"); // this should never happen but just in case
                    return AnyThread_sendApiError(request, ApiError::NotAllowedError, "amount is too big");
                }

                amounts.push_back({
                    {"amount",  beam::AmountBig::get_Lo(amount)},
                    {"assetID", sinfo.first},
                    {"spend",   false}
                });
            }

            info.push_back({"isEnough", isEnough});
            printApproveLog("Get user consent for contract tx", getAppId(), getAppName(), info, amounts);
            ClientThread_getContractConsent(request, info, amounts);
        }

        void ClientThread_prepareSendConsent(const std::string& request, IWalletApi::ParseResult& parse)
        {
            json info = {
                {"comment",  extractComment(parse)},
                {"fee",      parse.minfo.fee},
                {"token",    parse.minfo.token ? *parse.minfo.token : std::string()},
                {"isOnline", !parse.minfo.spendOffline},
                {"isSpend",  true}
            };

            const auto &spend = parse.minfo.spend;
            if (spend.size() != 1)
            {
                assert(!"tx_send must spend strictly 1 asset");
                return AnyThread_sendApiError(request, ApiError::NotAllowedError, "tx_send must spend strictly 1 asset");
            }

            const auto assetId = spend.begin()->first;
            const auto amount = spend.begin()->second;

            if (beam::AmountBig::get_Hi(amount) != 0)
            {
                assert("!amount is too big"); // this should never happen but just in case
                return AnyThread_sendApiError(request, ApiError::NotAllowedError, "amount is too big");
            }

            json amounts = json::array();
            amounts.push_back({
                {"amount",  beam::AmountBig::get_Lo(amount)},
                {"assetID", assetId},
                {"spend",   true}
            });

            _client->getAsync()->selectCoins(beam::AmountBig::get_Lo(amount), parse.minfo.fee, assetId, false,
                [this, wpsel = _weakSelf, request, info, amounts](const CoinsSelectionInfo& csi) mutable {
                    auto locked = wpsel.lock();
                    if (!locked)
                    {
                        // Can happen if user leaves the application
                        LOG_WARNING() << "UIT send CSI arrived but apps api is already destroyed";
                    }

                    info.push_back({"isEnough", csi.m_isEnought});
                    printApproveLog("Get user consent for send", getAppId(), getAppName(), info, amounts);
                    ClientThread_getSendConsent(request, info, amounts);
                });
        }

        // IWalletApiHandler
        void sendAPIResponse(const beam::wallet::json& result) override
        {
            // This is called in API (REACTOR) thread
            AnyThread_sendApiResponse(result);
        }

        const std::string _appId;
        const std::string _appName;

        // THIS IS UNSAFE but there is no way to get std::shared_ptr at the moment
        // Ensure that api is destroyed before client
        WalletClient* _client;

        // Weak reference to the topmost (Target)
        WeakPtr _weakSelf;

        // API should be accessed only in context of the reactor thread
        using ApiPtr = beam::wallet::IWalletApi::Ptr;
        ApiPtr _walletAPI;
        std::shared_ptr<ApiHandlerProxy> _walletAPIProxy;
    };
}
