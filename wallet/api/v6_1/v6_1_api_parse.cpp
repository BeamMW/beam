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
#include "v6_1_api.h"
#include "utility/fsutils.h"

namespace beam::wallet
{
    std::pair<EvSubUnsub, IWalletApi::MethodInfo> V61Api::onParseEvSubUnsub(const JsonRpcId& id, const nlohmann::json& params)
    {
        std::set<std::string> allowed;
        allowed.insert("ev_sync_progress");
        allowed.insert("ev_system_state");
        allowed.insert("ev_assets_changed");
        allowed.insert("ev_addrs_changed");
        allowed.insert("ev_utxos_changed");
        allowed.insert("ev_txs_changed");

        bool found = false;
        for (auto it : params.items())
        {
            if(allowed.find(it.key()) == allowed.end())
            {
                std::string error = "The event '" + it.key() + "' is unknown.";
                throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, error);
            }
            else
            {
                found = true;
            }

            if (!getAppId().empty())
            {
                // Some events are not allowed for applications
                if (it.key() == "ev_utxos_changed")
                {
                    throw jsonrpc_exception(ApiError::NotAllowedError);
                }
            }
        }

        if (found == false)
        {
            throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, "Must subunsub at least one supported event");
        }

        // So here we are sure that at least one known event is present
        // Typecheck & get value
        EvSubUnsub message;
        message.syncProgress = getOptionalParam<bool>(params, "ev_sync_progress");
        message.systemState  = getOptionalParam<bool>(params, "ev_system_state");
        message.assetChanged = getOptionalParam<bool>(params, "ev_assets_changed");
        message.addrsChanged = getOptionalParam<bool>(params, "ev_addrs_changed");
        message.utxosChanged = getOptionalParam<bool>(params, "ev_utxos_changed");
        message.txsChanged   = getOptionalParam<bool>(params, "ev_txs_changed");

        return std::make_pair(message, MethodInfo());
    }

    void V61Api::getResponse(const JsonRpcId& id, const EvSubUnsub::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", res.result}
        };
    }

    std::pair<GetVersion, IWalletApi::MethodInfo> V61Api::onParseGetVersion(const JsonRpcId& id, const nlohmann::json& params)
    {
        GetVersion message{};
        return std::make_pair(message, MethodInfo());
    }

    void V61Api::getResponse(const JsonRpcId& id, const GetVersion::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result",
                {
                    {"api_version",        res.apiVersion},
                    {"api_version_major",  res.apiVersionMajor},
                    {"api_version_minor",  res.apiVersionMinor},
                    {"beam_version",       res.beamVersion},
                    {"beam_version_major", res.beamVersionMajor},
                    {"beam_version_minor", res.beamVersionMinor},
                    {"beam_version_rev",   res.beamVersionRevision},
                    {"beam_commit_hash",   res.beamCommitHash},
                    {"beam_branch_name",   res.beamBranchName}
                }
            }
        };
    }

    std::pair<WalletStatusV61, IWalletApi::MethodInfo> V61Api::onParseWalletStatusV61(const JsonRpcId& id, const nlohmann::json& params)
    {
        WalletStatusV61 message{};
        message.nzOnly = getOptionalParam<bool>(params, "nz_totals");
        return std::make_pair(message, MethodInfo());
    }

    void V61Api::getResponse(const JsonRpcId& id, const WalletStatusV61::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result",
                {
                    {"current_height", res.currentHeight},
                    {"current_state_hash", to_hex(res.currentStateHash.m_pData, res.currentStateHash.nBytes)},
                    {"current_state_timestamp", res.currentStateTimestamp},
                    {"prev_state_hash", to_hex(res.prevStateHash.m_pData, res.prevStateHash.nBytes)},
                    {"is_in_sync", res.isInSync},
                    {"available",  res.available},
                    {"receiving",  res.receiving},
                    {"sending",    res.sending},
                    {"maturing",   res.maturing},
                    {"difficulty", res.difficulty}
                }
            }
        };

        if (res.totals)
        {
            for(const auto& it: res.totals->GetAllTotals())
            {
                const auto& totals = it.second;
                json jtotals;

                jtotals["asset_id"] = totals.AssetId;

                auto avail = totals.Avail; avail += totals.AvailShielded;
                jtotals["available_str"] = std::to_string(avail);

                if (avail <= kMaxAllowedInt)
                {
                    jtotals["available"] = AmountBig::get_Lo(avail);
                }

                auto incoming = totals.Incoming; incoming += totals.IncomingShielded;
                jtotals["receiving_str"] = std::to_string(incoming);

                if (totals.Incoming <= kMaxAllowedInt)
                {
                    jtotals["receiving"] = AmountBig::get_Lo(incoming);
                }

                auto outgoing = totals.Outgoing; outgoing += totals.OutgoingShielded;
                jtotals["sending_str"] = std::to_string(outgoing);

                if (totals.Outgoing <= kMaxAllowedInt)
                {
                    jtotals["sending"] = AmountBig::get_Lo(outgoing);
                }

                auto maturing = totals.Maturing; maturing += totals.MaturingShielded;
                jtotals["maturing_str"] = std::to_string(maturing);

                if (totals.Maturing <= kMaxAllowedInt)
                {
                    jtotals["maturing"] = AmountBig::get_Lo(maturing);
                }

                msg["result"]["totals"].push_back(jtotals);
            }
        }
    }

    std::pair<InvokeContractV61, IWalletApi::MethodInfo> V61Api::onParseInvokeContractV61(const JsonRpcId &id, const json &params)
    {
        InvokeContractV61 message;

        if(const auto contract = getOptionalParam<NonEmptyJsonArray>(params, "contract"))
        {
            const json& bytes = *contract;
            message.contract = bytes.get<std::vector<uint8_t>>();
        }
        else if(const auto fname = getOptionalParam<NonEmptyString>(params, "contract_file"))
        {
            fsutils::fread(*fname).swap(message.contract);
        }

        if (const auto args = getOptionalParam<NonEmptyString>(params, "args"))
        {
            message.args = *args;
        }

        if (const auto createTx = getOptionalParam<bool>(params, "create_tx"))
        {
            message.createTx = *createTx;
        }

        if (isApp() && message.createTx)
        {
            throw jsonrpc_exception(ApiError::NotAllowedError, "Applications must set create_tx to false and use process_contract_data");
        }

        if (const auto priority = getOptionalParam<PositiveUint32>(params, "priority"))
        {
            message.priority = *priority;
        }

        return std::make_pair(message, MethodInfo());
    }

    void V61Api::getResponse(const JsonRpcId& id, const InvokeContractV61::Response& res, json& msg)
    {
        msg = nlohmann::json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result",
                {
                    {"output", res.output ? *res.output : std::string("")}
                }
            }
        };

        if (res.txid)
        {
            msg["result"]["txid"] = std::to_string(*res.txid);
        }

        if (res.invokeData)
        {
            msg["result"]["raw_data"] = *res.invokeData;
        }
    }
}
