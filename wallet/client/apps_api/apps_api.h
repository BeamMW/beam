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
            , _client(nullptr)
        {
            _walletAPIProxy = std::make_shared<ApiHandlerProxy>();
        }

        ~AppsApi() override
        {
            getAsync()->makeIWTCall(
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
            BEAM_LOG_INFO () << "AppsApi destroyed for " << _appName << ", " << _appId;
        }

    public:
        typedef std::shared_ptr<Target> Ptr;
        typedef std::weak_ptr<Target> WeakPtr;

        static void ClientThread_Create(WalletClient* client,
                       std::string version,
                       std::string appid,
                       std::string appname,
                       uint32_t privilegeLvl,
                       bool ipfsnode,
                       std::function<void (Ptr)> cback)
        {
            if (client == nullptr)
            {
                assert(false);
                throw std::runtime_error("no client provided");
            }

            struct IWTResult
            {
                #ifdef BEAM_IPFS_SUPPORT
                IPFSService::Ptr ipfs;
                #endif
                IShadersManager::Ptr shaders;
            };

            client->getAsync()->makeIWTCall(
                [client, appid, appname, privilegeLvl, ipfsnode]() -> boost::any {
                    //
                    // THIS IS WALLET CLIENT REACTOR THREAD
                    //
                    IWTResult result;
                    bool hasIPFSNode = false;

                    #ifdef BEAM_IPFS_SUPPORT
                    if (ipfsnode) {
                        try
                        {
                            result.ipfs = client->IWThread_startIPFSNode();
                            hasIPFSNode = true;
                        }
                        catch(const std::runtime_error& err)
                        {
                            assert(false);
                            BEAM_LOG_ERROR() << "Failed to start IPFS node: " << err.what();
                        }
                    }
                    #else
                    ipfsnode;
                    #endif

                    if (!hasIPFSNode) {
                        BEAM_LOG_INFO() << "IPFS Node is not running. IPFS would not be available for the '"
                                   << appname << "' DApp";
                    }

                    result.shaders = client->IWThread_createAppShaders(appid, appname, privilegeLvl);
                    return result;
                },
                [client, cback=std::move(cback), version, appid, appname, privilegeLvl](boost::any aptr) {
                    //
                    // THIS IS UI THREAD
                    //
                    auto wapi = std::make_shared<Target>(appid, appname);
                    wapi->_walletAPIProxy->_handler = wapi;
                    wapi->_client = client;
                    wapi->_weakSelf = wapi;

                    ApiInitData data;
                    auto iwtres      = boost::any_cast<IWTResult>(aptr);
                    data.contracts   = std::move(iwtres.shaders);
                    data.swaps       = nullptr;
                    data.wallet      = client->getWallet();
                    data.walletDB    = client->getWalletDB();
                    data.appId       = appid;
                    data.appName     = appname;
                    data.nodeNetwork = client->getNodeNetwork();

                    #ifdef BEAM_IPFS_SUPPORT
                    data.ipfs = std::move(iwtres.ipfs);
                    #endif

                    wapi->_walletAPI = IWalletApi::CreateInstance(version, *wapi->_walletAPIProxy, data);
                    BEAM_LOG_INFO () << "AppsApi created for " << appid << ", " << appname << ", privileges " << privilegeLvl;
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
            //BEAM_LOG_INFO () << "AppsApi direct call for " << getAppName() << ", " << getAppId() << "): " << request;

            //
            // Do not assume thread here
            // Should be safe to call from any thread
            //
            makeIWTCallGuarded(
                [wp = _weakSelf, this, request]() -> boost::any {
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
                bool send = true;
                IWalletApi::ParseResult data;
            };

            // BEAM_LOG_INFO () << "AppsApi checked call for " << getAppName() << ", " << getAppId() << "): " << request;
            makeIWTCallGuarded(
                [this, request]() mutable -> boost::any {
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
                                BEAM_LOG_INFO() << "Application called method " << acinfo.method << " that spends funds, but user consent is not handled";
                                assert(false);

                                AnyThread_sendApiError(request, ApiError::NotAllowedError, std::string());
                                return {};
                            }

                            _walletAPI->executeAPIRequest(request.c_str(), request.size());
                            return {};
                        }

                        BEAM_LOG_INFO() << "Application requested call of the not allowed method: " << pres->acinfo.method;
                        AnyThread_sendApiError(request, ApiError::NotAllowedError, std::string());
                        return {};
                    }
                    else
                    {
                        // parse failed, just log error and return. Error response is already sent back
                        BEAM_LOG_ERROR() << "WebAPP API parse failed: " << request;
                        return {};
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
            if (!str.empty())
            {
                AnyThread_sendApiResponse(std::move(str));
            }
        }

        void AnyThread_sendApiError(const std::string& request, beam::wallet::ApiError err, const std::string& message)
        {
            //
            // Do not assume thread here
            // Should be safe to call from any thread
            //
            auto error = _walletAPI->fromError(request, err, message);
            AnyThread_sendApiResponse(std::move(error));
        }

        virtual void AnyThread_sendApiResponse(std::string&& result) = 0;
        virtual void ClientThread_getSendConsent(const std::string& request, const nlohmann::json& info, const nlohmann::json& amounts) = 0;
        virtual void ClientThread_getContractConsent(const std::string& request, const nlohmann::json& info, const nlohmann::json& amounts) = 0;

    private:

        void makeIWTCallGuarded(std::function<boost::any()>&& function, IWalletModelAsync::AsyncCallback<const boost::any&>&& resultCallback)
        {
            // this guard should prevent to destroy API object from incorrect thread
            auto apiGuard = std::make_shared<Ptr>();
            getAsync()->makeIWTCall(
                [apiGuard, wp = _weakSelf, function=std::move(function)]()->boost::any {
                    auto locked = wp.lock();
                    if (!locked)
                    {
                        // this means that api is disconnected and destroyed already
                        // well, okay, nothing to do then
                        return boost::none;
                    }
                    *apiGuard = locked;
                    return function();
                },
                [resultCallback=std::move(resultCallback)](const boost::any& any) {
                    resultCallback(any);
                }
            );
        }

        WalletClient* getClinet ()
        {
            if (_client != nullptr)
            {
                return _client;
            }
            assert (false);
            throw std::runtime_error("get on null _clinet");
        }

        IWalletModelAsync::Ptr getAsync()
        {
            return getClinet()->getAsync();
        }

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

            return {};
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
                isEnough = isEnough && (totalAmount <= getClinet()->getAvailable(assetId));
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

            getAsync()->selectCoins(beam::AmountBig::get_Lo(amount), parse.minfo.fee, assetId, false,
                [this, wpsel = _weakSelf, request, info, amounts](const CoinsSelectionInfo& csi) mutable {
                    auto locked = wpsel.lock();
                    if (!locked)
                    {
                        // Can happen if user leaves the application
                        BEAM_LOG_WARNING() << "UIT send CSI arrived but apps api is already destroyed";
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
