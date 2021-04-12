// Copyright 2018-2020 The Beam Team
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
#include "wallet/core/common_utils.h"
#include "wallet/core/simple_transaction.h"
#include "wallet/core/strings_resources.h"
#include "wallet/core/assets_utils.h"
#include "utility/logger.h"
#include "wallet_api.h"
#include "utility/test_helpers.h"

using namespace beam;
using namespace std::placeholders;

namespace
{
    using namespace beam::wallet;

    std::map<std::string, std::function<bool(const Coin& a, const Coin& b)>> utxoSortMap = 
    {
        {"id", [] (const Coin& a, const Coin& b) { return a.toStringID() < b.toStringID();} },
        {"asset_id", [] (const Coin& a, const Coin& b) { return a.m_ID.m_AssetID < b.m_ID.m_AssetID;}},
        {"amount", [] (const Coin& a, const Coin& b) { return a.m_ID.m_Value < b.m_ID.m_Value;}},
        {"type", [] (const Coin& a, const Coin& b) { return a.m_ID.m_Type < b.m_ID.m_Type;}},
        {"maturity", [] (const Coin& a, const Coin& b) { return a.get_Maturity() < b.get_Maturity();}},
        {"createTxId", [] (const Coin& a, const Coin& b)
            {
                std::string createTxIdA = a.m_createTxId.is_initialized() ? std::to_string(*a.m_createTxId) : "";
                std::string createTxIdB = b.m_createTxId.is_initialized() ? std::to_string(*b.m_createTxId) : "";
                return createTxIdA < createTxIdB;
            }},
        {"spentTxId", [] (const Coin& a, const Coin& b)
            {
                std::string spentTxIdA = a.m_spentTxId.is_initialized() ? std::to_string(*a.m_spentTxId) : "";
                std::string spentTxIdB = b.m_spentTxId.is_initialized() ? std::to_string(*b.m_spentTxId) : "";
                return spentTxIdA < spentTxIdB;
            }},
        {"status", [] (const Coin& a, const Coin& b) { return a.m_status < b.m_status;}},
        {"status_string", [] (const Coin& a, const Coin& b) { return a.getStatusString() < b.getStatusString();}}
    };

    WalletAddress::ExpirationStatus MapExpirationStatus(AddressData::Expiration exp)
    {
        switch (exp)
        {
        case EditAddress::OneDay:
            return WalletAddress::ExpirationStatus::OneDay;
        case EditAddress::Auto:
            return WalletAddress::ExpirationStatus::Auto;
        case EditAddress::Expired:
            return WalletAddress::ExpirationStatus::Expired;
        case EditAddress::Never:
            return WalletAddress::ExpirationStatus::Never;
        default:
            return WalletAddress::ExpirationStatus::Auto;
        }
    }

}  // namespace

namespace beam::wallet
{
    void WalletApi::FillAddressData(const AddressData& data, WalletAddress& address)
    {
        if (data.comment)
        {
            address.setLabel(*data.comment);
        }

        if (data.expiration)
        {
            switch (*data.expiration)
            {
            case EditAddress::Auto:
                address.setExpiration(WalletAddress::ExpirationStatus::Auto);
                break;
            case EditAddress::OneDay:
                address.setExpiration(WalletAddress::ExpirationStatus::OneDay);
                break;
            case EditAddress::Expired:
                address.setExpiration(WalletAddress::ExpirationStatus::Expired);
                break;
            case EditAddress::Never:
                address.setExpiration(WalletAddress::ExpirationStatus::Never);
                break;
            }
        }
    }

    void WalletApi::onHandleCalcMyChange(const JsonRpcId& id, const CalcMyChange& data)
    {
        LOG_DEBUG() << "CalcChange(id = " << id << " amount = " << data.amount << " asset_id = " << (data.assetId ? *data.assetId : 0) << ")";

        CoinsSelectionInfo csi = { 0 };

        csi.m_requestedSum = data.amount;
        if (data.assetId)
        {
            csi.m_assetID = *data.assetId;
        }

        csi.m_explicitFee = data.explicitFee;

        csi.Calculate(get_CurrentHeight(), getWalletDB(), data.isPushTransaction);

        doResponse(id, CalcMyChange::Response{ csi.m_changeBeam, csi.m_changeAsset, csi.m_explicitFee });
    }

    void WalletApi::onHandleChangePassword(const JsonRpcId& id, const ChangePassword& data)
    {
        LOG_DEBUG() << "ChangePassword(id = " << id << ")";

        getWalletDB()->changePassword(data.newPassword);
        doResponse(id, ChangePassword::Response{ });
    }

    void WalletApi::onHandleAddrList(const JsonRpcId& id, const AddrList& data)
    {
        LOG_DEBUG() << "AddrList(id = " << id << ")";

        auto walletDB = getWalletDB();
        auto all = walletDB->getAddresses(data.own);

        if (_appid.empty())
        {
            return doResponse(id, AddrList::Response{all});
        }

        decltype(all) filtered;
        for(const auto& addr: all)
        {
            if (_appid.empty() || addr.m_category == _appid)
            {
                filtered.push_back(addr);
            }
        }

        doResponse(id, AddrList::Response{filtered});
    }

    void WalletApi::onHandleCreateAddress(const JsonRpcId& id, const CreateAddress& data)
    {
        LOG_DEBUG() << "CreateAddress(id = " << id << ")";

        if (!getWallet()->IsConnectedToOwnNode()
           && (data.type == TokenType::MaxPrivacy
            || data.type == TokenType::Public
            || data.type == TokenType::Offline))
        {
            throw jsonrpc_exception(ApiError::NotSupported, "Must be connected to own node to generate this address type.");
        }

        auto walletDB = getWalletDB();

        WalletAddress address;
        walletDB->createAddress(address);

        if (data.comment) address.setLabel(*data.comment);
        if (data.expiration) address.setExpiration(*data.expiration);
        if (!_appid.empty()) address.setCategory(_appid);

        std::string newToken = GenerateToken(data.type, address,  walletDB, data.offlinePayments);
        walletDB->saveAddress(address);

        doResponse(id, CreateAddress::Response{newToken});
    }

    void WalletApi::onHandleDeleteAddress(const JsonRpcId& id, const DeleteAddress& data)
    {
        LOG_DEBUG() << "DeleteAddress(id = " << id << " address = " << std::to_string(data.address) << ")";

        auto walletDB = getWalletDB();
        auto addr = walletDB->getAddress(data.address);

        if (addr)
        {
            walletDB->deleteAddress(data.address);
            doResponse(id, DeleteAddress::Response{});
        }
        else
        {
            throw jsonrpc_exception(ApiError::InvalidAddress, "Provided address doesn't exist.");
        }
    }

    void WalletApi::onHandleEditAddress(const JsonRpcId& id, const EditAddress& data)
    {
        LOG_DEBUG() << "EditAddress(id = " << id << " address = " << std::to_string(data.address) << ")";

        auto walletDB = getWalletDB();
        auto addr = walletDB->getAddress(data.address);

        if (addr)
        {
            if (addr->isOwn())
            {
                FillAddressData(data, *addr);
                walletDB->saveAddress(*addr);
                doResponse(id, EditAddress::Response{});
            }
            else
            {
                throw jsonrpc_exception(ApiError::InvalidAddress, "You can edit only own address.");
            }
        }
        else
        {
            throw jsonrpc_exception(ApiError::InvalidAddress, "Provided address doesn't exist.");
        }
    }

    void WalletApi::onHandleValidateAddress(const JsonRpcId& id, const ValidateAddress& data)
    {
        LOG_DEBUG() << "ValidateAddress( address = " << data.address << ")";

        auto p = ParseParameters(data.address);

        bool isValid = !!p;
        bool isMine = false;

        if (p)
        {
            if (auto v = p->GetParameter<WalletID>(TxParameterID::PeerID); v)
            {
                isValid &= v->IsValid();

                auto walletDB = getWalletDB();
                auto addr = walletDB->getAddress(*v);

                if (addr)
                {
                    isMine = addr->isOwn();
                    if (isMine)
                    {
                        isValid = isValid && !addr->isExpired();
                    }
                }
            }
        }

        doResponse(id, ValidateAddress::Response{ isValid, isMine });
    }

    void WalletApi::doTxAlreadyExistsError(const JsonRpcId& id)
    {
        throw jsonrpc_exception(ApiError::InvalidTxId, "Provided transaction ID already exists in the wallet.");
    }

    void WalletApi::onHandleSend(const JsonRpcId& id, const Send& data)
    {
        auto token = data.txParameters.GetParameter<std::string>(beam::wallet::TxParameterID::OriginalToken);
        LOG_DEBUG() << "Send(id = " << id
                    << " asset_id = " << (data.assetId ? *data.assetId : 0)
                    << " amount = " << data.value
                    << " fee = " << data.fee
                    << " address = " << (token ? *token : std::to_string(data.address))
                    << ")";

        try
        {
            WalletID from(Zero);

            auto walletDB = getWalletDB();
            auto wallet = getWallet();

            if (data.from)
            {
                if (!data.from->IsValid())
                {
                    throw jsonrpc_exception(ApiError::InvalidAddress, "Invalid sender address.");
                }

                auto addr = walletDB->getAddress(*data.from);
                if (!addr || !addr->isOwn())
                {
                    throw jsonrpc_exception(ApiError::InvalidAddress, "It's not your own address.");
                }

                if (addr->isExpired())
                {
                    throw jsonrpc_exception(ApiError::InvalidAddress, "Sender address is expired.");
                }

                from = *data.from;
            }
            else
            {
                WalletAddress senderAddress;
                walletDB->createAddress(senderAddress);
                walletDB->saveAddress(senderAddress);
                from = senderAddress.m_walletID;
            }

            ByteBuffer message(data.comment.begin(), data.comment.end());
            CoinIDList coins;

            if (data.session)
            {
                coins = walletDB->getLockedCoins(*data.session);

                if (coins.empty())
                {
                    throw jsonrpc_exception(ApiError::InternalErrorJsonRpc, "Requested session is empty.");
                }
            }
            else
            {
                coins = data.coins ? *data.coins : CoinIDList();
            }

            if (data.txId && walletDB->getTx(*data.txId))
            {
                doTxAlreadyExistsError(id);
                return;
            }

            auto params = CreateSimpleTransactionParameters(data.txId);
            if (!LoadReceiverParams(data.txParameters, params, data.addressType))
            {
                throw jsonrpc_exception(ApiError::InternalErrorJsonRpc, "Cannot load transaction parameters.");
            }

            if (token)
            {
                params.SetParameter(beam::wallet::TxParameterID::OriginalToken, *token);
            }

            if(data.assetId)
            {
                params.SetParameter(TxParameterID::AssetID, *data.assetId);
            }

            params.SetParameter(TxParameterID::MyID, from)
                .SetParameter(TxParameterID::Amount, data.value)
                .SetParameter(TxParameterID::Fee, data.fee)
                .SetParameter(TxParameterID::PreselectedCoins, coins)
                .SetParameter(TxParameterID::Message, message);

            auto txId = wallet->StartTransaction(params);

            doResponse(id, Send::Response{ txId });
        }
        catch(const jsonrpc_exception&)
        {
            throw;
        }
        catch (...)
        {
            throw jsonrpc_exception(ApiError::InternalErrorJsonRpc, "Transaction could not be created. Please look at logs.");
        }
    }

    template<typename T>
    void WalletApi::setTxAssetParams(const JsonRpcId& id, TxParameters& params, const T& data)
    {
        auto walletDB = getWalletDB();

        if (data.assetMeta)
        {
            params.SetParameter(TxParameterID::AssetMetadata, *data.assetMeta);
        }
        else if (data.assetId)
        {
            if (const auto asset = walletDB->findAsset(*data.assetId))
            {
                std::string strmeta;
                asset->m_Metadata.get_String(strmeta);
                params.SetParameter(TxParameterID::AssetMetadata, strmeta);
            }
            else
            {
                throw jsonrpc_exception(ApiError::InternalErrorJsonRpc, "Asset not found in a local database. Update asset info (tx_asset_info) or provide asset metdata.");
            }
        }

        assert(!"presence of params should be checked already");
        throw jsonrpc_exception(ApiError::InternalErrorJsonRpc, "asset_id or meta is required");
    }

    template void WalletApi::setTxAssetParams(const JsonRpcId& id, TxParameters& params, const Issue& data);
    template void WalletApi::setTxAssetParams(const JsonRpcId& id, TxParameters& params, const Consume& data);

    void WalletApi::onHandleIssue(const JsonRpcId& id, const Issue& data)
    {
        onHandleIssueConsume(true, id, data);
    }

    void WalletApi::onHandleConsume(const JsonRpcId& id, const Consume& data)
    {
        onHandleIssueConsume(false, id, data);
    }

    template<typename T>
    void WalletApi::onHandleIssueConsume(bool issue, const JsonRpcId& id, const T& data)
    {
        LOG_DEBUG() << (issue ? " Issue" : " Consume") << "(id = " << id << " asset_id = "
                    << (data.assetId ? *data.assetId : 0)
                    << " asset_meta = " << (data.assetMeta ? *data.assetMeta : "\"\"")
                    << " amount = " << data.value << " fee = " << data.fee
                    << ")";
        try
        {
            CoinIDList coins;
            auto walletDB = getWalletDB();
            auto wallet = getWallet();

            if (data.session)
            {
                if ((coins = walletDB->getLockedCoins(*data.session)).empty())
                {
                    throw jsonrpc_exception(ApiError::InternalErrorJsonRpc, "Requested session is empty.");
                }
            } else
            {
                coins = data.coins ? *data.coins : CoinIDList();
            }

            if (data.txId && walletDB->getTx(*data.txId))
            {
                doTxAlreadyExistsError(id);
                return;
            }

            auto params = CreateTransactionParameters(issue ? TxType::AssetIssue : TxType::AssetConsume, data.txId)
                    .SetParameter(TxParameterID::Amount, data.value)
                    .SetParameter(TxParameterID::Fee, data.fee)
                    .SetParameter(TxParameterID::PreselectedCoins, coins);

            setTxAssetParams(id, params, data);
            const auto txId = wallet->StartTransaction(params);
            doResponse(id, Issue::Response{ txId });
        }
        catch(const jsonrpc_exception&)
        {
            throw;
        }
        catch (...)
        {
            throw jsonrpc_exception(ApiError::InternalErrorJsonRpc, "Transaction could not be created. Please look at logs.");
        }
    }

    template void WalletApi::onHandleIssueConsume<Issue>(bool issue, const JsonRpcId& id, const Issue& data);
    template void WalletApi::onHandleIssueConsume<Consume>(bool issue, const JsonRpcId& id, const Consume& data);

    void WalletApi::onHandleGetAssetInfo(const JsonRpcId &id, const GetAssetInfo &data)
    {
        LOG_DEBUG() << " GetAssetInfo" << "(id = " << id << " asset_id = "
                    << (data.assetId ? *data.assetId : 0)
                    << " asset_meta = " << (data.assetMeta ? *data.assetMeta : "\"\"")
                    << ")";

        auto walletDB = getWalletDB();
        boost::optional<WalletAsset> info;

        if (data.assetId)
        {
            info = walletDB->findAsset(*data.assetId);
        }
        else if (data.assetMeta)
        {
            const auto kdf = walletDB->get_MasterKdf();
            const auto ownerID = GetAssetOwnerID(kdf, *data.assetMeta);
            info = walletDB->findAsset(ownerID);
        }

        if(!info.is_initialized())
        {
            throw jsonrpc_exception(ApiError::InternalErrorJsonRpc, "Asset not found in a local database. Update asset info (tx_asset_info) and try again.");
        }

        GetAssetInfo::Response resp;
        resp.AssetInfo = *info;

        doResponse(id, resp);
    }

    void WalletApi::onHandleSetConfirmationsCount(const JsonRpcId& id, const SetConfirmationsCount& data)
    {
        auto walletDB = getWalletDB();
        walletDB->setCoinConfirmationsOffset(data.count);
        doResponse(id, GetConfirmationsCount::Response{ walletDB->getCoinConfirmationsOffset() });
    }

    void WalletApi::onHandleGetConfirmationsCount(const JsonRpcId& id, const GetConfirmationsCount& data)
    {
        auto walletDB = getWalletDB();
        doResponse(id, GetConfirmationsCount::Response{ walletDB->getCoinConfirmationsOffset() });
    }

    void WalletApi::onHandleTxAssetInfo(const JsonRpcId& id, const TxAssetInfo& data)
    {
        LOG_DEBUG() << " AssetInfo" << "(id = " << id << " asset_id = "
                    << (data.assetId ? *data.assetId : 0)
                    << " asset_meta = " << (data.assetMeta ? *data.assetMeta : "\"\"")
                    << ")";
        try
        {
            auto walletDB = getWalletDB();
            auto wallet = getWallet();

            if (data.txId && walletDB->getTx(*data.txId))
            {
                doTxAlreadyExistsError(id);
                return;
            }

            auto params = CreateTransactionParameters(TxType::AssetInfo, data.txId);
            if (data.assetMeta)
            {
                params.SetParameter(TxParameterID::AssetMetadata, *data.assetMeta);
            }
            else if (data.assetId)
            {
                params.SetParameter(TxParameterID::AssetID, *data.assetId);
            }
            else
            {
                throw jsonrpc_exception(ApiError::InternalErrorJsonRpc, "asset_id or meta is required");
            }

            const auto txId = wallet->StartTransaction(params);
            doResponse(id, Issue::Response{ txId });
        }
        catch(const jsonrpc_exception&)
        {
            throw;
        }
        catch (...)
        {
            throw jsonrpc_exception(ApiError::InternalErrorJsonRpc, "Transaction could not be created. Please look at logs.");
        }
    }

    void WalletApi::onHandleStatus(const JsonRpcId& id, const Status& data)
    {
        LOG_DEBUG() << "Status(txId = " << to_hex(data.txId.data(), data.txId.size()) << ")";

        auto walletDB = getWalletDB();
        auto tx = walletDB->getTx(data.txId);

        if (tx)
        {
            Block::SystemState::ID stateID = {};
            walletDB->getSystemStateID(stateID);

            Status::Response result;
            result.tx            = *tx;
            result.systemHeight  = stateID.m_Height;
            result.confirmations = 0;
            result.txHeight      = storage::DeduceTxProofHeight(*walletDB, *tx);

            doResponse(id, result);
        }
        else
        {
            throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, "Unknown transaction ID.");
        }
    }

    void WalletApi::onHandleSplit(const JsonRpcId& id, const Split& data)
    {
        LOG_DEBUG() << "Split(id = " << id
                    << " asset_id " << (data.assetId ? *data.assetId : 0)
                    << " coins = [";
        for (auto& coin : data.coins) LOG_DEBUG() << coin << ",";
        LOG_DEBUG() << "], fee = " << data.fee;

        try
        {
            auto walletDB = getWalletDB();
            auto wallet = getWallet();

            WalletAddress senderAddress;
            walletDB->createAddress(senderAddress);
            walletDB->saveAddress(senderAddress);

            if (data.txId && walletDB->getTx(*data.txId))
            {
                doTxAlreadyExistsError(id);
                return;
            }

            auto params = CreateSplitTransactionParameters(senderAddress.m_walletID, data.coins, data.txId)
                    .SetParameter(TxParameterID::Fee, data.fee);

            if (data.assetId)
            {
                params.SetParameter(TxParameterID::AssetID, *data.assetId);
            }

            auto txId = wallet->StartTransaction(params);
            doResponse(id, Send::Response{ txId });
        }
        catch(const jsonrpc_exception&)
        {
            throw;
        }
        catch (...)
        {
            throw jsonrpc_exception(ApiError::InternalErrorJsonRpc, "Transaction could not be created. Please look at logs.");
        }
    }

    void WalletApi::onHandleTxCancel(const JsonRpcId& id, const TxCancel& data)
    {
        LOG_DEBUG() << "TxCancel(txId = " << to_hex(data.txId.data(), data.txId.size()) << ")";

        auto walletDB = getWalletDB();
        auto wallet = getWallet();
        auto tx = walletDB->getTx(data.txId);

        if (tx)
        {
            if (wallet->CanCancelTransaction(tx->m_txId))
            {
                wallet->CancelTransaction(tx->m_txId);
                TxCancel::Response result{ true };
                doResponse(id, result);
            }
            else
            {
                throw jsonrpc_exception(ApiError::InvalidTxStatus, "Transaction could not be cancelled. Invalid transaction status.");
            }
        }
        else
        {
            throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, "Unknown transaction ID.");
        }
    }

    void WalletApi::onHandleTxDelete(const JsonRpcId& id, const TxDelete& data)
    {
        LOG_DEBUG() << "TxDelete(txId = " << to_hex(data.txId.data(), data.txId.size()) << ")";

        auto walletDB = getWalletDB();
        auto tx = walletDB->getTx(data.txId);

        if (tx)
        {
            if (tx->canDelete())
            {
                walletDB->deleteTx(data.txId);

                if (walletDB->getTx(data.txId))
                {
                    throw jsonrpc_exception(ApiError::InternalErrorJsonRpc, "Transaction not deleted.");
                }
                else
                {
                    doResponse(id, TxDelete::Response{ true });
                }
            }
            else
            {
                throw jsonrpc_exception(ApiError::InternalErrorJsonRpc, "Transaction can't be deleted.");
            }
        }
        else
        {
            throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, "Unknown transaction ID.");
        }
    }

    void WalletApi::onHandleGetUtxo(const JsonRpcId& id, const GetUtxo& data)
    {
        LOG_DEBUG() << "GetUtxo(id = " << id << " assets = " << data.withAssets
                    << " asset_id = " << (data.filter.assetId ? *data.filter.assetId : 0)
                    << ")";

        auto walletDB = getWalletDB();

        GetUtxo::Response response;
        response.confirmations_count = walletDB->getCoinConfirmationsOffset();
        walletDB->visitCoins([&response, &data](const Coin& c)->bool {
            if(!data.withAssets && c.isAsset())
            {
                return true;
            }
            if (data.filter.assetId && !c.isAsset(*data.filter.assetId))
            {
                return true;
            }
            response.utxos.push_back(c);
            return true;
        });

        if (data.sort.field != "default")
        {
            if (const auto& it = utxoSortMap.find(data.sort.field); it != utxoSortMap.end())
            {
                std::sort(response.utxos.begin(), response.utxos.end(),
                        data.sort.desc ? std::bind(it->second, _2, _1) : it->second);
            }
            else
            {
                throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, "Can't sort by \"" + data.sort.field + "\" field");
            }
        }
        else if (data.sort.desc)
        {
            std::reverse(response.utxos.begin(), response.utxos.end());
        }

        doPagination(data.skip, data.count, response.utxos);
        doResponse(id, response);
    }

    void WalletApi::onHandleWalletStatusApi(const JsonRpcId& id, const WalletStatusApi& data)
    {
        LOG_DEBUG() << "WalletStatus(id = " << id << ")";

        WalletStatusApi::Response response;
        auto walletDB = getWalletDB();

        {
            Block::SystemState::ID stateID = {};
            walletDB->getSystemStateID(stateID);

            response.currentHeight = stateID.m_Height;
            response.currentStateHash = stateID.m_Hash;
        }

        {
            Block::SystemState::Full state;
            walletDB->get_History().get_Tip(state);
            response.prevStateHash = state.m_Prev;
            response.difficulty = state.m_PoW.m_Difficulty.ToFloat();
        }

        storage::Totals allTotals(*walletDB);
        const auto& totals = allTotals.GetBeamTotals();

        response.available = AmountBig::get_Lo(totals.Avail);    response.available += AmountBig::get_Lo(totals.AvailShielded);
        response.receiving = AmountBig::get_Lo(totals.Incoming); response.receiving += AmountBig::get_Lo(totals.IncomingShielded);
        response.sending   = AmountBig::get_Lo(totals.Outgoing); response.sending   += AmountBig::get_Lo(totals.OutgoingShielded);
        response.maturing  = AmountBig::get_Lo(totals.Maturing); response.maturing  += AmountBig::get_Lo(totals.MaturingShielded);

        if (data.withAssets)
        {
            response.totals = allTotals;
        }

        doResponse(id, response);
    }

    void WalletApi::onHandleGenerateTxId(const JsonRpcId& id, const GenerateTxId& data)
    {
        LOG_DEBUG() << "GenerateTxId(id = " << id << ")";

        doResponse(id, GenerateTxId::Response{ wallet::GenerateTxID() });
    }

    void WalletApi::onHandleLock(const JsonRpcId& id, const Lock& data)
    {
        LOG_DEBUG() << "Lock(id = " << id << ")";

        Lock::Response response = {};
        auto walletDB = getWalletDB();
        response.result = walletDB->lockCoins(data.coins, data.session);

        doResponse(id, response);
    }

    void WalletApi::onHandleUnlock(const JsonRpcId& id, const Unlock& data)
    {
        LOG_DEBUG() << "Unlock(id = " << id << " session = " << data.session << ")";

        Unlock::Response response = {};
        auto walletDB = getWalletDB();
        response.result = walletDB->unlockCoins(data.session);

        doResponse(id, response);
    }

    void WalletApi::onHandleTxList(const JsonRpcId& id, const TxList& data)
    {
        LOG_DEBUG() << "List(filter.status = " << (data.filter.status ? std::to_string((uint32_t)*data.filter.status) : "nul") << ")";
        helpers::StopWatch sw;
        sw.start();
        TxList::Response res;

        {
            auto walletDB = getWalletDB();

            Block::SystemState::ID stateID = {};
            walletDB->getSystemStateID(stateID);
            res.resultList.reserve(data.count);
            uint32_t offset = 0;
            uint32_t counter = 0;

            TxListFilter filter;
            filter.m_AssetID = data.filter.assetId;
            filter.m_Status = data.filter.status;
            if (data.withAssets)
            {
                filter.m_AssetConfirmedHeight = data.filter.height;
            }
            filter.m_KernelProofHeight = data.filter.height;
            walletDB->visitTx([&](const TxDescription& tx)
            {
                // filter supported tx types
                // TODO: remove this in future, this condition was added to preserve existing behavior
                if (tx.m_txType    != TxType::Simple
                    && tx.m_txType != TxType::PushTransaction
                    && tx.m_txType != TxType::AssetIssue
                    && tx.m_txType != TxType::AssetConsume
                    && tx.m_txType != TxType::AssetInfo
                    && tx.m_txType != TxType::AssetReg
                    && tx.m_txType != TxType::AssetUnreg
                    && tx.m_txType != TxType::Contract)
                {
                    return true;
                }

                if (tx.m_assetId > 0 && !data.withAssets)
                {
                    return true;
                }

                ++offset;
                if (offset <= data.skip)
                {
                    return true;
                }
                const auto height = storage::DeduceTxProofHeight(*walletDB, tx);
                Status::Response& item = res.resultList.emplace_back();
                item.tx = tx;
                item.txHeight = height;
                item.systemHeight = stateID.m_Height;
                item.confirmations = 0;

                ++counter;
                return data.count == 0 || counter < data.count;
            }, filter);
            assert(data.count == 0 || (uint32_t)res.resultList.size() <= data.count);
        }
        
        doResponse(id, res);
        sw.stop();
        LOG_DEBUG() << "TxList  elapsed time: " << sw.milliseconds() << " ms\n";
    }

    void WalletApi::onHandleExportPaymentProof(const JsonRpcId& id, const ExportPaymentProof& data)
    {
        LOG_DEBUG() << "ExportPaymentProof(id = " << id << ")";

        auto walletDB = getWalletDB();
        auto tx = walletDB->getTx(data.txId);

        if (!tx)
        {
            throw jsonrpc_exception(ApiError::PaymentProofExportError, kErrorPpExportFailed);
        }
        else if (!tx->m_sender || tx->m_selfTx)
        {
            throw jsonrpc_exception(ApiError::PaymentProofExportError, kErrorPpCannotExportForReceiver);
        }
        else if (tx->m_status != TxStatus::Completed)
        {
            throw jsonrpc_exception(ApiError::PaymentProofExportError, kErrorPpExportFailedTxNotCompleted);
        }
        else
        {
            doResponse(id, ExportPaymentProof::Response{ wallet::storage::ExportPaymentProof(*walletDB, data.txId) });
        }
    }

    void WalletApi::onHandleVerifyPaymentProof(const JsonRpcId &id, const VerifyPaymentProof &data)
    {
        LOG_DEBUG() << "VerifyPaymentProof(id = " << id << ")";
        try
        {
            doResponse(id, VerifyPaymentProof::Response{ storage::PaymentInfo::FromByteBuffer(data.paymentProof) });
        }
        catch (...)
        {
            throw jsonrpc_exception(ApiError::InvalidPaymentProof, "Failed to parse");
        }
    }

    void WalletApi::onHandleInvokeContract(const JsonRpcId &id, const InvokeContract &data)
    {
        LOG_DEBUG() << "InvokeContract(id = " << id << ")";
        auto contracts = getContracts();

        if (!contracts->IsDone())
        {
            throw jsonrpc_exception(ApiError::UnexpectedError, "Previous shader call is still in progress");
        }

        try
        {
            if (!data.contract.empty())
            {
                contracts->CompileAppShader(data.contract);
            }
        }
        catch(std::runtime_error& err)
        {
            throw jsonrpc_exception(ApiError::ContractCompileError, err.what());
        }

        try
        {
            _ccallId = id;
            std::weak_ptr<bool> wguard = _contractsGuard;

            contracts->Start(data.args, data.args.empty() ? 0 : 1, [this, wguard](boost::optional<TxID> txid, boost::optional<std::string> result, boost::optional<std::string> error) {
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
                    return sendError(_ccallId, ApiError::ContractError, *error);
                }

                InvokeContract::Response response;
                response.output = result ? *result : "";
                response.txid   = txid ? *txid : TxID();

                const auto callid = _ccallId;
                _ccallId = JsonRpcId();

                doResponse(callid, response);
            });
        }
        catch(std::runtime_error& err)
        {
            throw jsonrpc_exception(ApiError::ContractError, err.what());
        }
    }

    void WalletApi::onHandleBlockDetails(const JsonRpcId& id, const BlockDetails& data)
    {
        LOG_DEBUG() << "InvokeContract(id = " << id << ")";

        _requestHandler.requestHeader(data.blockHeight, id);
    }

    WalletApi::RequestHandler::RequestHandler(WalletApi& parent)
        : _parent(parent)
    {
    }

    void WalletApi::RequestHandler::OnComplete(proto::FlyClient::Request& request)
    {
        RequestHeaderMsg& headerRequest = dynamic_cast<RequestHeaderMsg&>(request);

        _requestList.erase(RequestHeaderList::s_iterator_to(headerRequest));
        headerRequest.m_pTrg = nullptr;
        headerRequest.Release();

        if (headerRequest.m_vStates.size() != 1)
        {
            _parent.sendError(headerRequest._id, ApiError::InternalErrorJsonRpc, "Cannot get block header.");
            return;
        }

        Block::SystemState::Full state = headerRequest.m_vStates.front();
        Merkle::Hash blockHash;
        state.get_Hash(blockHash);

        std::string rulesHash = Rules::get().pForks[_countof(Rules::get().pForks) - 1].m_Hash.str();

        for (size_t i = 1; i < _countof(Rules::get().pForks); i++)
        {
            if (state.m_Height < Rules::get().pForks[i].m_Height)
            {
                rulesHash = Rules::get().pForks[i - 1].m_Hash.str();
                break;
            }
        }

        BlockDetails::Response response;
        response.height = state.m_Height;
        response.blockHash = blockHash.str();
        response.previousBlock = state.m_Prev.str();
        response.chainwork = state.m_ChainWork.str();
        response.kernels = state.m_Kernels.str();
        response.definition = state.m_Definition.str();
        response.timestamp = state.m_TimeStamp;
        response.pow = beam::to_hex(&state.m_PoW, sizeof(state.m_PoW));
        response.difficulty = state.m_PoW.m_Difficulty.ToFloat();
        response.packedDifficulty = state.m_PoW.m_Difficulty.m_Packed;
        response.rulesHash = rulesHash;

        _parent.doResponse(headerRequest._id, response);
    }

    void WalletApi::RequestHandler::requestHeader(Height blockHeight, const JsonRpcId& id)
    {
        RequestHeaderMsg::Ptr request(new RequestHeaderMsg);

        request->m_Msg.m_Height = blockHeight;
        request->_id = id;
        request->AddRef();

        _requestList.push_back(*request);

        _parent.getWallet()->GetNodeEndpoint()->PostRequest(*request, *this);
    }
}
