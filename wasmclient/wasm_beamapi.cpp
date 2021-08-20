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
#include "common.h"
#include "wasm_beamapi.h"
#include <boost/algorithm/string.hpp>
#include "utility/string_helpers.h"
#include "core/block_crypt.h"

using namespace beam;
using namespace beam::wallet;

namespace
{
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

WasmAppApi::WasmAppApi(const std::string appid, const std::string appname)
    : AppsApi<WasmAppApi>(std::move(appid), std::move(appname))
{
}

void WasmAppApi::CallWalletAPI(const std::string& request)
{
    AssertMainThread();
    // 'call checked' means that API will check if funds are to be spent
    // and would raise approval sequence in this case
    AnyThread_callWalletApiChecked(request);
}

void WasmAppApi::SetResultHandler(emscripten::val handler)
{
    AssertMainThread();
    m_jsResultReceiver = std::make_unique<emscripten::val>(std::move(handler));
}

void WasmAppApi::SetSendConsentHandler(ClientThread_SendConsentHandler handler)
{
    m_ctSendConsent = std::move(handler);
}

void WasmAppApi::SetContractConsentHandler(ClientThread_ContractConsentHandler handler)
{
    m_ctContractConsent = std::move(handler);
}

void WasmAppApi::SetPostToClientHandler(AnyThread_PostHandler handler)
{
    m_postToClient = std::move(handler);
}

void WasmAppApi::AnyThread_sendApiResponse(const std::string& result)
{
    if (!m_postToClient)
    {
        throw std::runtime_error("m_postToClient missing");
    }

    WeakPtr wp = shared_from_this();
    std::function<void (void)> execInCT = [this, wp, result]()
    {
        if (auto sp = wp.lock())
        {
            if (m_jsResultReceiver && !m_jsResultReceiver->isNull())
            {
                (*m_jsResultReceiver)(result);
            }
        }
    };
    m_postToClient(execInCT);
}

void WasmAppApi::ClientThread_getContractConsent(const std::string& request, const nlohmann::json& oinfo, const nlohmann::json& oamounts)
{
    AssertMainThread();
    if (!m_ctContractConsent)
    {
        throw std::runtime_error("m_ctContractConsentConsent missing");
    }

    nlohmann::json info = oinfo;

    auto fee = oinfo["fee"].get<Amount>();
    info["fee"] = beamAmountToUIString(fee);

    auto amounts = nlohmann::json::array();
    for(const auto& am: oamounts)
    {
        auto amount = am["amount"].get<Amount>();
        amounts.push_back({
            {"amount",  beamAmountToUIString(amount)},
            {"spend",   am["spend"].get<bool>()},
            {"assetID", am["assetID"].get<Asset::ID>()}
        });
    }

    m_ctContractConsent(request, info.dump(), amounts.dump());
}

void WasmAppApi::ClientThread_getSendConsent(const std::string& request, const nlohmann::json& oinfo, const nlohmann::json& oamounts)
{
    AssertMainThread();
    if (!m_ctSendConsent)
    {
        throw std::runtime_error("m_ctSendConsent missing");
    }

    nlohmann::json info = oinfo;

    auto fee = oinfo["fee"].get<Amount>();
    info["fee"] = beamAmountToUIString(fee);

    if (!oamounts.is_array() || oamounts.size() != 1)
    {
        assert(!"send must spend strictly 1 asset");
        return AnyThread_sendApiError(request, ApiError::NotAllowedError, "send must spend strictly 1 asset");
    }

    const auto amount = (*oamounts.begin())["amount"].get<Amount>();
    info.push_back({"amount", std::to_string(PrintableAmount(amount))});

    const auto assetID = (*oamounts.begin())["assetID"].get<Asset::ID>();
    info.push_back({"assetID", assetID});

    m_ctSendConsent(request, info.dump());
}
