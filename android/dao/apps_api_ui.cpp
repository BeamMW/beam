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
#include "apps_api_ui.h"
#include <boost/regex.hpp>
#include <boost/optional.hpp>
#include "utility/logger.h"
#include "utility/bridge.h"
#include "utility/io/asyncevent.h"
#include "utility/helpers.h"
#include "utility/common.h"
#include <sstream>
#include <jni.h>
#include "../common.h"

AppsApiUI::AppsApiUI(const std::string& appid, const std::string& appname) : AppsApi<AppsApiUI>(appid, appname)
{
}

int AppsApiUI::test()
{
    // only for test, always 42
    return 42;
}

void AppsApiUI::sendApproved(const std::string& request)
{
    LOG_INFO() << "Contract send approved: " << getAppName() << ", " << getAppId() << ", " << request;
    AnyThread_callWalletApiDirectly(request);
}

void AppsApiUI::sendRejected(const std::string& request)
{
    LOG_INFO() << "Contract send rejected: " << getAppName() << ", " << getAppId() << ", " << request;
    AnyThread_sendApiError(request, beam::wallet::ApiError::UserRejected, std::string());
}

void AppsApiUI::contractInfoApproved(const std::string& request)
{
    LOG_INFO() << "Contract tx approved: " << getAppName() << ", " << getAppId() << ", " << request;
    AnyThread_callWalletApiDirectly(request);
}

void AppsApiUI::contractInfoRejected(const std::string& request)
{
    LOG_INFO() << "Contract tx rejected: " << getAppName() << ", " << getAppId() << ", " << request;
    AnyThread_sendApiError(request, beam::wallet::ApiError::UserRejected, std::string());
}

void AppsApiUI::callWalletApi(const std::string& request)
{
    LOG_INFO() << "Call Wallet Api: " << getAppName() << ", " << getAppId() << ", " << request;
    AnyThread_callWalletApiChecked(request);
}

void AppsApiUI::AnyThread_sendApiResponse(std::string&& result)
{
    LOG_INFO() << "Send Api Response: " << getAppName() << ", " << getAppId() << ", " << result;

    JNIEnv* env = Android_JNI_getEnv();

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "sendDAOApiResult", "(Ljava/lang/String;)V");

    jstring jdata = env->NewStringUTF(result.c_str());

    env->CallStaticVoidMethod(WalletListenerClass, callback, jdata);
    env->DeleteLocalRef(jdata);
}

void AppsApiUI::ClientThread_getSendConsent(const std::string& request, const nlohmann::json& jinfo, const nlohmann::json& jamounts)
{
    if (!jamounts.is_array() || jamounts.size() != 1)
    {
        assert(false);
        return AnyThread_sendApiError(request, beam::wallet::ApiError::NotAllowedError, "send must spend strictly 1 asset");
    }
    
    const auto info = prepareInfo4QT(jinfo);
    const auto amounts = prepareAmounts4QT(jamounts);
    
    JNIEnv* env = Android_JNI_getEnv();

    jobject contractConset = env->AllocObject(ContractConsetClass);
    {   
        setStringField(env, ContractConsetClass, contractConset, "request", request);
        setStringField(env, ContractConsetClass, contractConset, "info", info);
        setStringField(env, ContractConsetClass, contractConset, "amounts", amounts);
    }

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "approveSend", "(L" BEAM_JAVA_PATH "/entities/dto/ContractConsentDTO;)V");
    env->CallStaticVoidMethod(WalletListenerClass, callback, contractConset);
    env->DeleteLocalRef(contractConset);
}

void AppsApiUI::ClientThread_getContractConsent(const std::string& request, const nlohmann::json& jinfo, const nlohmann::json& jamounts)
{
    const auto info = prepareInfo4QT(jinfo);
    const auto amounts = prepareAmounts4QT(jamounts);
   

    JNIEnv* env = Android_JNI_getEnv();

    jobject contractConset = env->AllocObject(ContractConsetClass);
    {   
        setStringField(env, ContractConsetClass, contractConset, "request", request);
        setStringField(env, ContractConsetClass, contractConset, "info", info);
        setStringField(env, ContractConsetClass, contractConset, "amounts", amounts);
    }

    jmethodID callback = env->GetStaticMethodID(WalletListenerClass, "approveContractInfo", "(L" BEAM_JAVA_PATH "/entities/dto/ContractConsentDTO;)V");
    env->CallStaticVoidMethod(WalletListenerClass, callback, contractConset);
    env->DeleteLocalRef(contractConset);
}

std::string AppsApiUI::prepareInfo4QT(const nlohmann::json& info)
{
    nlohmann::json result = nlohmann::json::object();
    for (const auto& kv: info.items())
    {
        if (kv.key() == "fee")
        {
            const auto fee = AmountToUIString(info["fee"].get<beam::Amount>());
            result["fee"] = fee;
            
            const auto feeRate = ""; 
            result["feeRate"]  = feeRate;
            result["rateUnit"] =  "";
        }
        else
        {
            result.push_back({kv.key(), kv.value()});
        }
    }
    
    return result.dump();
}

std::string AppsApiUI::prepareAmounts4QT(const nlohmann::json& amounts)
{
    nlohmann::json result = nlohmann::json::array();
    for (const auto& val: amounts)
    {
        const auto assetId = val["assetID"].get<beam::Asset::ID>();
        const auto amount = val["amount"].get<beam::Amount>();
        const auto spend = val["spend"].get<bool>();
        
        result.push_back({
            {"assetID", assetId},
            {"amount", AmountToUIString(amount)},
            {"spend", spend}
        });
    }
    
    return result.dump();
}

std::string AppsApiUI::AmountToUIString(const beam::Amount& value)
{
    double realAmount = (double(int64_t(beam::AmountBig::get_Lo(value))) / beam::Rules::Coin);
    return std::to_string(realAmount);
}
