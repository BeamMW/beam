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

#include "wasm_beamapi.h"
#include "utility/logger.h"
#include "wallet/client/wallet_client.h"

#include <sstream>
#include <boost/algorithm/string.hpp>

#include "3rdparty/nlohmann/json.hpp"

using json = nlohmann::json;

namespace beam::applications {
    using namespace beam::wallet;

    namespace
    {
        namespace
        {
            void printMap(const std::string& prefix, const json& info)
            {
                for(const auto& item : info.items())
                {
                    LOG_INFO () << prefix << item.key() << "=" << item.value();
                }
            }

            void printApproveLog(const std::string& preamble, const std::string& appid, const std::string& appname, const json& info, const json& amounts)
            {
                LOG_INFO() << preamble << " (" << appname << ", " << appid << "):";
                printMap("\t", info);

                if (!amounts.empty())
                {
                    for (const auto &amountMap : amounts)
                    {
                        LOG_INFO() << "\tamount entry:";
                        printMap("\t\t", amountMap);
                    }
                }
            }

            template <char C>
            bool char_is(const char c)
            {
                return c == C;
            }

            std::string encodeBase10(uint64_t amount, uint8_t decimalPlaces)
            {
                std::ostringstream stream;
                stream << std::setfill('0') << std::setw(1 + decimalPlaces) << amount;

                auto string = stream.str();
                string.insert(string.size() - decimalPlaces, 1, '.');
                boost::algorithm::trim_right_if(string, char_is<'0'>);
                boost::algorithm::trim_right_if(string, char_is<'.'>);
                return string;
            }

            std::string beamAmountToUIString(Amount value)
            {
                static auto beamDecimals = static_cast<uint8_t>(std::log10(Rules::Coin));
                return encodeBase10(value, beamDecimals);
            }
        }
    }

    WasmBeamApi::WasmBeamApi(WalletClientPtr wc, IWalletDB::Ptr db, beam::wallet::IShadersManager::Ptr shaders, const std::string& version, const std::string& appid, const std::string& appname)
        : AppsApi(appid, appname)
        , _client(wc)
       // , _amgr(AppModel::getInstance().getAssets())
    {
       // connect(_amgr.get(), &AssetsManager::assetsListChanged, this, &WasmBeamApi::assetsChanged);

        //
        // THIS IS UI THREAD
        //
        ApiInitData data;

        data.contracts = std::move(shaders);
        data.swaps     = nullptr;
        data.wallet    = _client->getWallet();//AppModel::getInstance().getWalletModel()->getWallet();
        data.walletDB = db; // AppModel::getInstance().getWalletDB();
        data.appId     = appid;
        data.appName   = appname;

        _walletAPIProxy = std::make_shared<ApiHandlerProxy>();
        _walletAPI = IWalletApi::CreateInstance(version, *_walletAPIProxy, data);
        LOG_INFO () << "WasmBeamApi created for " << data.appName << ", " << data.appId;
    }

    //std::shared_ptr<WasmBeamApi> WasmBeamApi::Create(const std::string& version, const std::string& appid, const std::string& appname)
    //{
    //    auto result = std::make_shared<WasmBeamApi>(version, appid, appname);
    //    result->_walletAPIProxy->_handler = result;
    //    return result;
    //}

    WasmBeamApi::~WasmBeamApi()
    {
        LOG_INFO () << "WasmBeamApi Destroyed";

        //
        // THIS IS UI THREAD
        //
        getAsyncWallet().makeIWTCall(
            [proxy = std::move(_walletAPIProxy), api = std::move(_walletAPI)] () mutable -> boost::any {
                // api should be destroyed in context of the wallet thread
                // it is ASSUMED to be the last call in api calls chain
                api.reset();
                proxy.reset();
                return boost::none;
            },
        [] (const boost::any&){
        });
    }

    //QMap<QString, QVariant> WasmBeamApi::getAssets()
    //{
    //    return _amgr->getAssetsMap(_mappedAssets);
    //}

    void WasmBeamApi::callWalletApi(const std::string& request)
    {
        //
        // THIS IS UI THREAD
        //
        LOG_INFO () << "WebAPP API call for " << getAppName() << ", " << getAppId() << "): " << request;

        std::weak_ptr<WasmBeamApi> wp = shared_from_this();
        getAsyncWallet().makeIWTCall(
            [wp, this, request]() -> boost::any {
                auto locked = wp.lock();
                if (!locked)
                {
                    // this means that api is disconnected and destroyed already, this is normal
                    return boost::none;
                }

                if (auto pres = _walletAPI->parseAPIRequest(request.c_str(), request.size()); pres)
                {
                    const auto& acinfo = pres->acinfo;
                    if (acinfo.appsAllowed)
                    {
                        if (acinfo.method == "tx_send")
                        {
                            AnyThread_getSendConsent(request, *pres);
                            return boost::none;
                        }

                        if (acinfo.method == "process_invoke_data")
                        {
                            AnyThread_getContractInfoConsent(request, *pres);
                            return boost::none;
                        }

                        if (pres->minfo.fee > 0 || !pres->minfo.spend.empty())
                        {
                            LOG_INFO() << "Application called method " << acinfo.method << " that spends funds, but user consent is not handled";
                            assert(false);

                            const auto error = _walletAPI->fromError(request, ApiError::NotAllowedError, std::string());
                            onCallWalletApiResult(error);

                            return boost::none;
                        }

                        _walletAPI->executeAPIRequest(request.c_str(), request.size());
                        return boost::none;
                    }

                    LOG_INFO() << "Application requested call of the not allowed method: " << pres->acinfo.method;
                    AnyThread_sendError(request, ApiError::NotAllowedError, std::string());
                    return boost::none;
                }
                else
                {
                    // parse failed, just log error and return. Error response is already sent back
                    LOG_ERROR() << "WebAPP API parse failed: " << request;
                    return boost::none;
                }
            },
            [] (const boost::any&) {
            }
        );
    }

    void WasmBeamApi::UIThread_callWalletApiImp(const std::string& request)
    {
        //
        // Do not assume thread here
        // Should be safe to call from any thread
        //
        std::weak_ptr<WasmBeamApi> wp = shared_from_this();
        getAsyncWallet().makeIWTCall(
            [wp, this, request]() -> boost::any {
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

    void WasmBeamApi::sendAPIResponse(const json& result)
    {
        //
        // This is reactor thread
        //
        AnyThread_sendAPIResponse(result);
        LOG_INFO() << "sendAPIResponse: " << result.dump();
    }

    void WasmBeamApi::AnyThread_sendAPIResponse(const beam::wallet::json& result)
    {
        //
        // Do not assume thread here
        // Should be safe to call from any thread
        //
        auto str = result.dump();
        LOG_INFO() << "AnyThread_sendAPIResponse: " << str;
        if (!str.empty())
        {
            onCallWalletApiResult(str);
        }
    }

    void WasmBeamApi::AnyThread_sendError(const std::string& request, beam::wallet::ApiError err, const std::string& message)
    {
        //
        // Do not assume thread here
        // Should be safe to call from any thread
        //
        const auto error = _walletAPI->fromError(request, err, message);
        onCallWalletApiResult(error);
    }

    void WasmBeamApi::AnyThread_getSendConsent(const std::string& request, const beam::wallet::IWalletApi::ParseResult& pinfo)
    {
        std::weak_ptr<WasmBeamApi> wp = shared_from_this();
        getAsyncWallet().makeIWTCall([] () -> boost::any {return boost::none;},
            [this, wp, request, pinfo](const boost::any&)
            {
                auto locked = wp.lock();
                if (!locked)
                {
                    // Can happen if user leaves the application
                    LOG_WARNING() << "AT -> UIT send consent arrived but creator is already destroyed";
                }

                //
                // THIS IS THE UI THREAD
                //
                decltype(_mappedAssets)().swap(_mappedAssets);
                _mappedAssets.insert(beam::Asset::s_BeamID);

                //
                // Do not assume thread here
                // Should be safe to call from any thread
                //
                const auto &spend = pinfo.minfo.spend;

                if (spend.size() != 1)
                {
                    assert(!"tx_send must spend strictly 1 asset");
                    return AnyThread_sendError(request, ApiError::NotAllowedError, "tx_send must spend strictly 1 asset");
                }

                const auto assetId = spend.begin()->first;
                const auto amount = spend.begin()->second;
                const auto fee = pinfo.minfo.fee;
                _mappedAssets.insert(assetId);

                json info =
                {
                    {"amount",     std::to_string(PrintableAmount(amount))},
                    {"fee",        beamAmountToUIString(fee)},
                    //info.insert("feeRate",    _amgr->getRate(beam::Asset::s_BeamID));
                    {"assetID",    assetId},
                    //info.insert("rateUnit",   _amgr->getRateUnit());
                    {"token",      pinfo.minfo.token ? *pinfo.minfo.token : std::string()},
                    {"isOnline",   !pinfo.minfo.spendOffline }
                };

                std::string comment = pinfo.minfo.confirm_comment ? *pinfo.minfo.confirm_comment : (pinfo.minfo.comment ? *pinfo.minfo.comment : std::string());
                info.push_back({ "comment", comment });


                std::weak_ptr<WasmBeamApi> wpsel = shared_from_this();
                getAsyncWallet().selectCoins(beam::AmountBig::get_Lo(amount), fee, assetId, false, [this, wpsel, request, info](const CoinsSelectionInfo& csi) mutable
                {
                    auto locked = wpsel.lock();
                    if (!locked)
                    {
                        // Can happen if user leaves the application
                        LOG_WARNING() << "UIT send CSI arrived but creator is already destroyed";
                    }

                    info.push_back({ "isEnough", csi.m_isEnought });
                    printApproveLog("Get user consent for send", getAppId(), getAppName(), info, {});
                    onApproveSend(request, info.dump());
                });
            }
        );
    }

    void WasmBeamApi::AnyThread_getContractInfoConsent(const std::string &request, const beam::wallet::IWalletApi::ParseResult& pinfo)
    {
        std::weak_ptr<WasmBeamApi> wp = shared_from_this();
        getAsyncWallet().makeIWTCall([] () -> boost::any {return boost::none;},
            [this, wp, request, pinfo](const boost::any&)
            {
                auto locked = wp.lock();
                if (!locked)
                {
                    // Can happen if user leaves the application
                    LOG_WARNING() << "AT -> UIT contract consent arrived but creator is already destroyed";
                }

                //
                // THIS IS THE UI THREAD
                //
                decltype(_mappedAssets)().swap(_mappedAssets);
                _mappedAssets.insert(beam::Asset::s_BeamID);

                json info =
                {
                    {"comment",   pinfo.minfo.confirm_comment ? *pinfo.minfo.confirm_comment : (pinfo.minfo.comment ? *pinfo.minfo.comment : std::string())},
                    {"fee",       beamAmountToUIString(pinfo.minfo.fee)},
                    //info.emplace("feeRate",         AmountToUIString(_amgr->getRate(beam::Asset::s_BeamID)));
                    //info.emplace("rateUnit",        _amgr->getRateUnit());
                    {"isSpend",   !pinfo.minfo.spend.empty()}
                };
                
                

                bool isEnough = true;

                json amounts = json::array();
                for(const auto& sinfo: pinfo.minfo.spend)
                {
                    const auto assetId = sinfo.first;
                    const auto amount  = sinfo.second;

                    _mappedAssets.insert(assetId);
                    json entry =
                    {
                        {"amount",   std::to_string(PrintableAmount(amount))},
                        {"assetID",  assetId},
                        {"spend",    true}
                    };
                    
                    amounts.push_back(entry);

                    auto totalAmount = amount;
                    if (assetId == beam::Asset::s_BeamID)
                    {
                        totalAmount += beam::AmountBig::Type(pinfo.minfo.fee);
                    }

                    isEnough = isEnough && (totalAmount <= _client->getAvailable(assetId));
                }

                for(const auto& sinfo: pinfo.minfo.receive)
                {
                    const auto assetId = sinfo.first;
                    const auto amount  = sinfo.second;

                    _mappedAssets.insert(assetId);
                    json entry =
                    {
                        {"amount",  std::to_string(PrintableAmount(amount))},
                        {"assetID", assetId},
                        {"spend", false}
                    };

                    amounts.push_back(entry);
                }

                info["isEnough"] = isEnough;
                printApproveLog("Get user consent for contract tx", getAppId(), getAppName(), info, amounts);
                onApproveContractInfo(request, info.dump(), amounts.dump());
            }
        );
    }

    void WasmBeamApi::sendApproved(const std::string& request)
    {
        //
        // This is UI thread
        //
        LOG_INFO() << "Contract tx approved: " << getAppName() << ", " << getAppId() << ", " << request;
        UIThread_callWalletApiImp(request);
    }

    void WasmBeamApi::sendRejected(const std::string& request)
    {
        //
        // This is UI thread
        //
        LOG_INFO() << "Contract tx rejected: " << getAppName() << ", " << getAppId() << ", " << request;
        AnyThread_sendError(request, beam::wallet::ApiError::UserRejected, std::string());
    }

    void WasmBeamApi::contractInfoApproved(const std::string& request)
    {
        //
        // This is UI thread
        //
        LOG_INFO() << "Contract tx approved: " << getAppName() << ", " << getAppId() << ", " << request;
        UIThread_callWalletApiImp(request);
    }

    void WasmBeamApi::contractInfoRejected(const std::string& request)
    {
        //
        // This is UI thread
        //
        LOG_INFO() << "Contract tx rejected: " << getAppName() << ", " << getAppId() << ", " << request;
        AnyThread_sendError(request, beam::wallet::ApiError::UserRejected, std::string());
    }

    IWalletModelAsync& WasmBeamApi::getAsyncWallet()
    {
        assert(_client);
        return *_client->getAsync();
    }
}
