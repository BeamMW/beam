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
#include "version.h"

namespace beam::wallet
{
    void V61Api::onHandleEvSubUnsub(const JsonRpcId &id, EvSubUnsub&& data)
    {
        auto oldSubs = _evSubs;

        if (data.systemState.is_initialized())
        {
            _evSubs = *data.systemState ? _evSubs | SubFlags::SystemState : _evSubs & ~SubFlags::SystemState;
        }

        if (data.syncProgress.is_initialized())
        {
            _evSubs = *data.syncProgress ? _evSubs | SubFlags::SyncProgress : _evSubs & ~SubFlags::SyncProgress;
        }

        if (data.assetChanged.is_initialized())
        {
            _evSubs = *data.assetChanged ? _evSubs | SubFlags::AssetChanged : _evSubs & ~SubFlags::AssetChanged;
        }

        if (data.utxosChanged.is_initialized())
        {
            _evSubs = *data.utxosChanged ? _evSubs | SubFlags::CoinsChanged : _evSubs & ~SubFlags::CoinsChanged;
        }

        if (data.addrsChanged.is_initialized())
        {
            _evSubs = *data.addrsChanged ? _evSubs | SubFlags::AddrsChanged : _evSubs & ~SubFlags::AddrsChanged;
        }

        if (data.txsChanged.is_initialized())
        {
            _evSubs = *data.txsChanged ? _evSubs | SubFlags::TXsChanged : _evSubs & ~SubFlags::TXsChanged;
        }

        if (_evSubs && !_subscribedToListener)
        {
            getWallet()->Subscribe(this);
            _subscribedToListener = true;
        }

        EvSubUnsub::Response resp{true};
        doResponse(id, resp);

        //
        // Generate all newly subscribed events immediately
        // This is convenient for subscribers to get initial system state
        //
        if ((_evSubs & SubFlags::SystemState) != 0 && (oldSubs & SubFlags::SystemState) == 0)
        {
            Block::SystemState::ID stateID = {};
            getWalletDB()->getSystemStateID(stateID);
            onSystemStateChanged(stateID);
        }

        if ((_evSubs & SubFlags::SyncProgress) != 0 && (oldSubs & SubFlags::SyncProgress) == 0)
        {
            onSyncProgress(0, 0);
        }

        if ((_evSubs & SubFlags::AssetChanged) != 0 && (oldSubs & SubFlags::AssetChanged) == 0)
        {
            std::vector<Asset::ID> ids;
            getWalletDB()->visitAssets([&ids](const WalletAsset& info) -> bool {
                ids.push_back(info.m_ID);
                return true;
            });
            onAssetsChanged(ChangeAction::Reset, ids);
        }

        if ((_evSubs & SubFlags::CoinsChanged) != 0 && (oldSubs & SubFlags::CoinsChanged) == 0)
        {
            std::vector<ApiCoin> coins;

            auto processCoin = [&](const auto& c) -> bool {
                ApiCoin::EmplaceCoin(coins, c);
                return true;
            };

            getWalletDB()->visitCoins(processCoin);
            getWalletDB()->visitShieldedCoins(processCoin);
            onCoinsChangedImp(ChangeAction::Reset, coins);
        };

        if ((_evSubs & SubFlags::AddrsChanged) != 0 && (oldSubs & SubFlags::AddrsChanged) == 0)
        {
            auto addrs = getWalletDB()->getAddresses(true);
            auto addrs2 = getWalletDB()->getAddresses(false);
            addrs.reserve(addrs.size() + addrs2.size());
            addrs.insert(addrs.end(), addrs2.begin(), addrs2.end());

            onAddressChanged(ChangeAction::Reset, addrs);
        }

        if ((_evSubs & SubFlags::TXsChanged) != 0 && (oldSubs & SubFlags::TXsChanged) == 0)
        {
            std::vector<TxDescription> txs;
            getWalletDB()->visitTx([&](const TxDescription& tx) -> bool {
                txs.push_back(tx);
                return true;
            }, TxListFilter());

            onTransactionChanged(ChangeAction::Reset, txs);
        }
    }

    void V61Api::onHandleGetVersion(const JsonRpcId& id, GetVersion&& params)
    {
        GetVersion::Response resp;

        resp.apiVersion          = _apiVersion;
        resp.apiVersionMajor     = _apiVersionMajor;
        resp.apiVersionMinor     = _apiVersionMinor;
        resp.beamVersion         = PROJECT_VERSION;
        resp.beamVersionMajor    = VERSION_MAJOR;
        resp.beamVersionMinor    = VERSION_MINOR;
        resp.beamVersionRevision = VERSION_REVISION;
        resp.beamCommitHash      = GIT_COMMIT_HASH;
        resp.beamBranchName      = BRANCH_NAME;

        doResponse(id, resp);
    }

    void V61Api::onHandleWalletStatusV61(const JsonRpcId &id, WalletStatusV61&& data)
    {
        LOG_DEBUG() << "WalletStatusV61(id = " << id << ")";

        WalletStatusV61::Response response;
        auto walletDB = getWalletDB();

        {
            Block::SystemState::ID stateID = {};
            walletDB->getSystemStateID(stateID);
            response.currentHeight = stateID.m_Height;
            response.currentStateHash = stateID.m_Hash;

            Block::SystemState::Full state;
            walletDB->get_History().get_At(state, stateID.m_Height);

            Merkle::Hash stateHash;
            state.get_Hash(stateHash);
            assert(stateID.m_Hash == stateHash);

            response.prevStateHash = state.m_Prev;
            response.difficulty = state.m_PoW.m_Difficulty.ToFloat();
            response.currentStateTimestamp = state.m_TimeStamp;
            response.isInSync = IsValidTimeStamp(state.m_TimeStamp);
        }

        storage::Totals allTotals(*walletDB, data.nzOnly.is_initialized() ? *data.nzOnly : false);
        const auto& totals = allTotals.GetBeamTotals();

        response.available = AmountBig::get_Lo(totals.Avail);    response.available += AmountBig::get_Lo(totals.AvailShielded);
        response.receiving = AmountBig::get_Lo(totals.Incoming); response.receiving += AmountBig::get_Lo(totals.IncomingShielded);
        response.sending   = AmountBig::get_Lo(totals.Outgoing); response.sending   += AmountBig::get_Lo(totals.OutgoingShielded);
        response.maturing  = AmountBig::get_Lo(totals.Maturing); response.maturing  += AmountBig::get_Lo(totals.MaturingShielded);

        if (getCAEnabled())
        {
            response.totals = allTotals;
        }

        doResponse(id, response);
    }

    void V61Api::onHandleInvokeContractV61(const JsonRpcId &id, InvokeContractV61&& data)
    {
        LOG_VERBOSE() << "InvokeContract(id = " << id << ")";
        auto contracts = getContracts();

        if (data.createTx)
        {
            onHandleInvokeContractWithTX(id, std::move(data));
        }
        else
        {
            onHandleInvokeContractNoTX(id, std::move(data));
        }
    }

    void V61Api::onHandleInvokeContractWithTX(const JsonRpcId &id, InvokeContractV61&& data)
    {
        getContracts()->CallShaderAndStartTx(data.contract, data.args, data.args.empty() ? 0 : 1, data.priority, data.unique,
        [this, id, wguard = _weakSelf](const boost::optional<TxID>& txid, boost::optional<std::string>&& result, boost::optional<std::string>&& error) {
            auto guard = wguard.lock();
            if (!guard)
            {
                LOG_WARNING() << "API destroyed before shader response received.";
                return;
            }

            //
            // N.B
            //  - you cannot freely throw here, this function is not guarded by the parseJSON checks,
            //    exceptions are not automatically processed and errors are not automatically pushed to the invoker
            //  - this function is called in the reactor context
            //
            if (error)
            {
                return sendError(id, ApiError::ContractError, *error);
            }

            InvokeContractV61::Response response;
            response.output = result ? *result : "";
            response.txid   = txid ? *txid : TxID();

            doResponse(id, response);
        });
    }

    void V61Api::onHandleInvokeContractNoTX(const JsonRpcId &id, InvokeContractV61&& data)
    {
        getContracts()->CallShader(data.contract, data.args, data.args.empty() ? 0 : 1, data.priority, data.unique,
        [this, id, wguard = _weakSelf](boost::optional<ByteBuffer>&& data, boost::optional<std::string>&& output, boost::optional<std::string>&& error) {
            auto guard = wguard.lock();
            if (!guard)
            {
                LOG_WARNING() << "API destroyed before shader response received.";
                return;
            }

            //
            // N.B
            //  - you cannot freely throw here, this function is not guarded by the parseJSON checks,
            //    exceptions are not automatically processed and errors are not automatically pushed to the invoker
            //  - this function is called in the reactor context
            //
            if (error)
            {
                return sendError(id, ApiError::ContractError, *error);
            }

            InvokeContractV61::Response response;
            response.output = std::move(output);
            response.invokeData = std::move(data);

            doResponse(id, response);
        });
    }
}
