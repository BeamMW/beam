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

namespace beam::wallet
{
    namespace
    {
        void fillSystemState(json &parent, const Block::SystemState::ID &stateID, IWalletDB::Ptr walletDB)
        {
            Block::SystemState::Full state, tip;
            walletDB->get_History().get_At(state, stateID.m_Height);
            walletDB->get_History().get_Tip(tip);

            Merkle::Hash stateHash, tipHash;
            state.get_Hash(stateHash);
            tip.get_Hash(tipHash);
            assert(stateID.m_Hash == stateHash);

            parent["current_height"] = stateID.m_Height;
            parent["current_state_hash"] = to_hex(stateHash.m_pData, stateHash.nBytes);
            parent["current_state_timestamp"] = state.m_TimeStamp;
            parent["prev_state_hash"] = to_hex(state.m_Prev.m_pData, state.m_Prev.nBytes);
            parent["is_in_sync"] = IsValidTimeStamp(state.m_TimeStamp);
            parent["tip_height"] = tip.m_Height;
            parent["tip_state_hash"] = to_hex(tipHash.m_pData, tipHash.nBytes);
            parent["tip_state_timestamp"] = tip.m_TimeStamp;
            parent["tip_prev_state_hash"] = to_hex(tip.m_Prev.m_pData, tip.m_Prev.nBytes);
        }
    }

    void V61Api::onSyncProgress(int done, int total)
    {
        if ((_evSubs & SubFlags::SyncProgress) == 0)
        {
            return;
        }

        // THIS METHOD IS NOT GUARDED
        try
        {
            auto walletDB = getWalletDB();

            Block::SystemState::ID stateID = {};
            walletDB->getSystemStateID(stateID);

            json msg = json
            {
                {JsonRpcHeader, JsonRpcVersion},
                {"id", "ev_sync_progress"},
                {"result",
                    {
                        {"done", done},
                        {"total", total}
                    }
                }
            };

            fillSystemState(msg["result"], stateID, walletDB);
            _handler.sendAPIResponse(msg);
        }
        catch(std::exception& e)
        {
            LOG_ERROR() << "V61Api::onSyncProgress failed: " << e.what();
        }
    }

    void V61Api::onSystemStateChanged(const Block::SystemState::ID& stateID)
    {
        if ((_evSubs & SubFlags::SystemState) == 0)
        {
            return;
        }

        // THIS METHOD IS NOT GUARDED
        try
        {
            json msg = json
            {
                {JsonRpcHeader, JsonRpcVersion},
                {"id", "ev_system_state"},
                {"result", {}}
            };

            fillSystemState(msg["result"], stateID, getWalletDB());
            _handler.sendAPIResponse(msg);
        }
        catch(std::exception& e)
        {
            LOG_ERROR() << "V61Api::onSystemStateChanged failed: " << e.what();
        }
    }

    void V61Api::fillAssetInfo(json& res, const WalletAsset& info)
    {
        // V6 behavior
        V6Api::fillAssetInfo(res, info);

        // V6.1 additions
        WalletAssetMeta meta(info);
        auto pairs = meta.GetMetaMap();

        res["metadata_kv"]  = !pairs.empty();
        res["metadata_std_min"] = meta.isStd_v5_0();
        res["metadata_std"] = meta.isStd();

        if (!pairs.empty())
        {
            res["metadata_pairs"] = json::object();
            for (auto& pair: pairs)
            {
                res["metadata_pairs"][pair.first] = pair.second;
            }
        }
    }

    void V61Api::onAssetsChanged(ChangeAction action, const std::vector<Asset::ID>& ids)
    {
        try
        {
            json msg = json
            {
                {JsonRpcHeader, JsonRpcVersion},
                {"id", "ev_assets_changed"},
                {"result",
                    {
                        {"change", action},
                        {"change_str", std::to_string(action)},
                        {"assets", json::array()}
                    }
                }
            };

            auto& arr = msg["result"]["assets"];
            if (action == ChangeAction::Removed)
            {
                for (auto aid: ids)
                {
                    arr.push_back({
                        {"asset_id", aid}
                    });
                }
            }
            else
            {
                for (auto aid: ids)
                {
                    if (const auto oasset = getWalletDB()->findAsset(aid))
                    {
                        auto obj = json::object();
                        fillAssetInfo(obj, *oasset);
                        arr.push_back(obj);
                    }
                    else
                    {
                        LOG_WARNING() << "onAssetsChanged: failed to find asset " << aid << ", action" << std::to_string(action);
                    }
                }
            }

            // allow reset even if empty
            // do not notify for other actions if empty
            if (action == ChangeAction::Reset || !arr.empty())
            {
                _handler.sendAPIResponse(msg);
            }
        }
        catch(std::exception& e)
        {
            LOG_ERROR() << "V61Api::onAssetsChanged failed: " << e.what();
        }
    }

    void V61Api::onAssetChanged(ChangeAction action, beam::Asset::ID assetId)
    {
        if ((_evSubs & SubFlags::AssetChanged) == 0)
        {
            return;
        }

        try
        {
            std::vector<Asset::ID> ids;
            ids.push_back(assetId);
            onAssetsChanged(action, ids);
        }
        catch(std::exception& e)
        {
            LOG_ERROR() << "V61Api::onAssetChanged failed: " << e.what();
        }
    }

    template<>
    void V61Api::onCoinsChangedImp(ChangeAction action, const std::vector<ApiCoin>& coins)
    {
        json msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", "ev_utxos_changed"},
            {"result",
                {
                    {"change", action},
                    {"change_str", std::to_string(action)},
                    {"utxos", json::array()}
                }
            }
        };

        // allow reset even if empty
        // do not notify for other actions if empty
        if (action == ChangeAction::Reset || !coins.empty())
        {
            fillCoins(msg["result"]["utxos"], coins);
            _handler.sendAPIResponse(msg);
        }
    }

    template<typename T>
    void V61Api::onCoinsChangedImp(ChangeAction action, const std::vector<T>& changed)
    {
        try
        {
            if ((_evSubs & SubFlags::CoinsChanged) == 0)
            {
                return;
            }

            const auto cCnt = getWalletDB()->getCoinConfirmationsOffset();
            std::vector<ApiCoin> coins;

            for (const auto &c: changed)
            {
                ApiCoin::EmplaceCoin(coins, c, cCnt);
            }

            onCoinsChangedImp(action, coins);
        }
        catch(std::exception& e)
        {
            LOG_ERROR() << "V61Api::onCoinsChangedImp failed: " << e.what();
        }
    }

    void V61Api::onCoinsChanged(ChangeAction action, const std::vector<Coin>& changed)
    {
        onCoinsChangedImp<Coin>(action, changed);
    }

    void V61Api::onShieldedCoinsChanged(ChangeAction action, const std::vector<ShieldedCoin>& changed)
    {
        onCoinsChangedImp<ShieldedCoin>(action, changed);
    }

    void V61Api::onAddressChanged(ChangeAction action, const std::vector<WalletAddress>& items)
    {
        if ((_evSubs & SubFlags::AddrsChanged) == 0)
        {
            return;
        }

        try
        {
            json msg = json
            {
                {JsonRpcHeader, JsonRpcVersion},
                {"id", "ev_addrs_changed"},
                {"result",
                    {
                        {"change", action},
                        {"change_str", std::to_string(action)},
                        {"addrs", json::array()}
                    }
                }
            };

            auto notify = [&](const std::vector<WalletAddress>& list) {
                // allow reset even if empty
                // do not notify for other actions if empty
                if (action == ChangeAction::Reset || !list.empty())
                {
                    fillAddresses(msg["result"]["addrs"], list);
                    _handler.sendAPIResponse(msg);
                }
            };

            const auto& appid = getAppId();
            if (appid.empty())
            {
                return notify(items);
            }

            std::vector<WalletAddress> filtered;
            std::copy_if(
                items.begin(),
                items.end(),
                std::back_inserter(filtered),
                [appid](const auto& addr) -> bool
                {
                    return addr.m_category == appid;
                }
            );
            notify(filtered);
        }
        catch(std::exception& e)
        {
            LOG_ERROR() << "V61Api::onAddressChanged failed: " << e.what();
        }
    }

    void V61Api::onTransactionChanged(ChangeAction action, const std::vector<TxDescription>& changed)
    {
        if ((_evSubs & SubFlags::TXsChanged) == 0)
        {
            return;
        }

        try
        {
            json msg = json
            {
                {JsonRpcHeader, JsonRpcVersion},
                {"id", "ev_txs_changed"},
                {"result",
                    {
                        {"change", action},
                        {"change_str", std::to_string(action)},
                        {"txs", json::array()}
                    }
                }
            };

            auto walletDB = getWalletDB();
            Block::SystemState::ID stateID = {};
            walletDB->getSystemStateID(stateID);

            std::vector<Status::Response> items;
            for(const auto& tx: changed)
            {
                if (allowedTx(tx))
                {
                    Status::Response &item = items.emplace_back();
                    item.tx = tx;
                    item.txHeight = storage::DeduceTxProofHeight(*walletDB, tx);
                    item.systemHeight = stateID.m_Height;
                }
            }

            // allow reset even if empty
            // do not notify for other actions if empty
            if (action == ChangeAction::Reset || !items.empty())
            {
                fillTransactions(msg["result"]["txs"], items);
                _handler.sendAPIResponse(msg);
            }
        }
        catch(std::exception& e)
        {
            LOG_ERROR() << "V61Api::onTransactionChanged failed: " << e.what();
        }
    }
}
