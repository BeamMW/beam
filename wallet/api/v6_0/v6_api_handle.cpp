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
#include "v6_api.h"
#include "utility/test_helpers.h"

using namespace beam;
using namespace std::placeholders;

namespace
{
    using namespace beam::wallet;

    std::map<std::string, std::function<bool(const ApiCoin& a, const ApiCoin& b)>> utxoSortMap =
    {
        #define MACRO(name, type) {#name, [](const auto& a, const auto& b) {return a.name < b.name;}},
        BEAM_GET_UTXO_RESPONSE_FIELDS(MACRO)
        #undef MACRO
    };

    const char* kAddrDoesntExistError = "Provided address doesn't exist.";
    const char* kUnknownTxID = "Unknown transaction ID.";
}

namespace beam::wallet
{
    void V6Api::FillAddressData(const AddressData& data, WalletAddress& address)
    {
        if (data.comment)
        {
            address.setLabel(*data.comment);
        }

        if (data.expiration)
        {
            address.setExpirationStatus(*data.expiration);
        }
    }

    void V6Api::onHandleCalcChange(const JsonRpcId& id, CalcChange&& data)
    {
        LOG_DEBUG() << "CalcChange(id = " << id << " amount = " << data.amount << " asset_id = " << (data.assetId ? *data.assetId : 0) << ")";

        CoinsSelectionInfo csi = {0};

        csi.m_requestedSum = data.amount;
        if (data.assetId)
        {
            csi.m_assetID = *data.assetId;
        }

        csi.m_explicitFee = data.explicitFee;
        csi.Calculate(get_TipHeight(), getWalletDB(), data.isPushTransaction);

        doResponse(id, CalcChange::Response{ csi.m_changeBeam, csi.m_changeAsset, csi.m_explicitFee });
    }

    void V6Api::onHandleChangePassword(const JsonRpcId& id, ChangePassword&& data)
    {
        LOG_DEBUG() << "ChangePassword(id = " << id << ")";

        getWalletDB()->changePassword(data.newPassword);
        doResponse(id, ChangePassword::Response{ });
    }

    void V6Api::onHandleAddrList(const JsonRpcId& id, AddrList&& data)
    {
        LOG_DEBUG() << "AddrList(id = " << id << ")";

        auto walletDB = getWalletDB();
        auto all = walletDB->getAddresses(data.own);

        if (!isApp())
        {
            return doResponse(id, AddrList::Response{all});
        }

        decltype(all) filtered;
        for(const auto& addr: all)
        {
            if (!isApp() || addr.m_category == getAppName())
            {
                filtered.push_back(addr);
            }
        }

        doResponse(id, AddrList::Response{filtered});
    }

    void V6Api::onHandleCreateAddress(const JsonRpcId& id, CreateAddress&& data)
    {
        LOG_DEBUG() << "CreateAddress(id = " << id << ")";

        if (!getWallet()->CanDetectCoins()
           && (data.type == TokenType::MaxPrivacy
            || data.type == TokenType::Public
            || data.type == TokenType::Offline))
        {
            throw jsonrpc_exception(ApiError::NotSupported, "Wallet must be connected to own node or mobile node protocol should be turned on to generate this address type.");
        }

        auto walletDB = getWalletDB();

        WalletAddress address;
        walletDB->createAddress(address);

        if (data.comment)    address.setLabel(*data.comment);
        if (data.expiration) address.setExpirationStatus(*data.expiration);
        if (isApp())         address.setCategory(getAppId());

        address.m_Address = GenerateToken(data.type, address,  walletDB, data.offlinePayments);
        walletDB->saveAddress(address);

        doResponse(id, CreateAddress::Response{address.m_Address});
    }

    void V6Api::onHandleDeleteAddress(const JsonRpcId& id, DeleteAddress&& data)
    {
        LOG_DEBUG() << "DeleteAddress(id = " << id << " address = " << data.token << ")";

        auto walletDB = getWalletDB();
        auto addr = walletDB->getAddressByToken(data.token);

        if (addr)
        {
            if (isApp() && addr->m_category != getAppId())
            {
                // we do not throw 'NotAllowed' to not to expose that address exists
                throw jsonrpc_exception(ApiError::InvalidAddress, kAddrDoesntExistError);
            }

            walletDB->deleteAddressByToken(data.token);
            doResponse(id, DeleteAddress::Response{});
        }
        else
        {
            throw jsonrpc_exception(ApiError::InvalidAddress, kAddrDoesntExistError);
        }
    }

    void V6Api::onHandleEditAddress(const JsonRpcId& id, EditAddress&& data)
    {
        LOG_DEBUG() << "EditAddress(id = " << id << " address = " << data.token << ")";

        auto walletDB = getWalletDB();
        auto addr = walletDB->getAddressByToken(data.token);

        if (addr)
        {
            if (isApp() && addr->m_category != getAppId())
            {
                // we do not throw 'NotAllowed' to not to expose that address exists
                throw jsonrpc_exception(ApiError::InvalidAddress, kAddrDoesntExistError);
            }

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
            throw jsonrpc_exception(ApiError::InvalidAddress, kAddrDoesntExistError);
        }
    }

    void V6Api::onHandleValidateAddress(const JsonRpcId& id, ValidateAddress&& data)
    {
        LOG_DEBUG() << "ValidateAddress( address = " << data.token << ")";

        auto tokenType = GetTokenType(data.token);
        auto p = ParseParameters(data.token);

        bool isValid = !!p;
        bool isMine = false;
        boost::optional<size_t> offlinePayments;
        boost::optional<WalletAddress> addr;

        if (p)
        {
            auto walletDB = getWalletDB();
            if (auto v = p->GetParameter<WalletID>(TxParameterID::PeerID); v)
            {
                isValid &= v->IsValid();
                addr = walletDB->getAddress(*v);
                if (tokenType == TokenType::Offline)
                {
                    if (auto vouchers = p->GetParameter<ShieldedVoucherList>(TxParameterID::ShieldedVoucherList); vouchers)
                    {
                        storage::SaveVouchers(*walletDB, *vouchers, *v);
                    }
                    offlinePayments = walletDB->getVoucherCount(*v);
                }
            }
            else
            {
                addr = walletDB->getAddressByToken(data.token);
            }
            if (addr)
            {
                isMine = addr->isOwn();
                if (isMine)
                {
                    isValid = isValid && !addr->isExpired();
                }
            }
        }

        doResponse(id, ValidateAddress::Response{ isValid, isMine, tokenType, offlinePayments });
    }

    void V6Api::doTxAlreadyExistsError(const JsonRpcId& id)
    {
        throw jsonrpc_exception(ApiError::InvalidTxId, "Provided transaction ID already exists in the wallet.");
    }

    void V6Api::onHandleSend(const JsonRpcId& id, Send&& data)
    {
        LOG_DEBUG() << "Send(id = "   << id
                    << " asset_id = " << (data.assetId ? *data.assetId : 0)
                    << " amount = "   << data.value
                    << " fee = "      << data.fee
                    << " from = "     << (data.tokenFrom ? *data.tokenFrom: "")
                    << " address = "  << data.tokenTo
                    << ")";

        try
        {
            WalletID from(Zero);

            auto walletDB = getWalletDB();
            auto wallet = getWallet();

            if (data.tokenFrom)
            {
                auto addr = walletDB->getAddressByToken(*data.tokenFrom);

                if (!addr.is_initialized())
                {
                    throw jsonrpc_exception(ApiError::InvalidAddress, "Unable to find sender address (from).");
                }

                if (!addr->m_walletID.IsValid())
                {
                    throw jsonrpc_exception(ApiError::InvalidAddress, "Invalid sender (from) address.");
                }

                if (!addr->isOwn())
                {
                    throw jsonrpc_exception(ApiError::InvalidAddress, "Sender address (from) is not your own address.");
                }

                if (addr->isExpired())
                {
                    throw jsonrpc_exception(ApiError::InvalidAddress, "Sender address (from) is expired.");
                }

                from = addr->m_walletID;
            }
            else
            {
                WalletAddress senderAddress;
                walletDB->createAddress(senderAddress);

                if (isApp())
                {
                    // This address is created for DApp
                    // Make it visible to DApp
                    senderAddress.setCategory(getAppId());
                }

                walletDB->saveAddress(senderAddress);
                from = senderAddress.m_walletID;
            }

            ByteBuffer message(data.comment.begin(), data.comment.end());
            CoinIDList coins = data.coins ? *data.coins : CoinIDList();

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

            if(data.assetId)
            {
                params.SetParameter(TxParameterID::AssetID, *data.assetId);
            }

            params.SetParameter(TxParameterID::MyID, from)
                .SetParameter(TxParameterID::Amount, data.value)
                .SetParameter(TxParameterID::Fee, data.fee)
                .SetParameter(TxParameterID::PreselectedCoins, coins)
                .SetParameter(TxParameterID::Message, message)
                .SetParameter(beam::wallet::TxParameterID::OriginalToken, data.tokenTo);

            if (isApp())
            {
                params.SetParameter(TxParameterID::AppID, getAppId());
                params.SetParameter(TxParameterID::AppName, getAppName());
            }

            auto txId = wallet->StartTransaction(params);
            doResponse(id, Send::Response{txId});
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
    void V6Api::setTxAssetParams(const JsonRpcId& id, TxParameters& params, const T& data)
    {
        auto walletDB = getWalletDB();

        if (const auto asset = walletDB->findAsset(data.assetId))
        {
            std::string strmeta;
            asset->m_Metadata.get_String(strmeta);
            params.SetParameter(TxParameterID::AssetMetadata, strmeta);
        }
        else
        {
            throw jsonrpc_exception(ApiError::InternalErrorJsonRpc, "Asset not found in the local database. Update asset info (tx_asset_info) and repeat your API call.");
        }
    }

    template void V6Api::setTxAssetParams(const JsonRpcId& id, TxParameters& params, const Issue& data);
    template void V6Api::setTxAssetParams(const JsonRpcId& id, TxParameters& params, const Consume& data);

    void V6Api::onHandleIssue(const JsonRpcId& id, Issue&& data)
    {
        onHandleIssueConsume(true, id, std::move(data));
    }

    void V6Api::onHandleConsume(const JsonRpcId& id, Consume&& data)
    {
        onHandleIssueConsume(false, id, std::move(data));
    }

    template<typename T>
    void V6Api::onHandleIssueConsume(bool issue, const JsonRpcId& id, T&& data)
    {
        LOG_DEBUG() << (issue ? " Issue" : " Consume") << "(id = " << id << " asset_id = " << data.assetId
                    << " amount = " << data.value << " fee = " << data.fee
                    << ")";
        try
        {
            auto walletDB = getWalletDB();
            auto wallet = getWallet();
            CoinIDList coins = data.coins ? *data.coins : CoinIDList();

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

    template void V6Api::onHandleIssueConsume<Issue>(bool issue, const JsonRpcId& id, Issue&& data);
    template void V6Api::onHandleIssueConsume<Consume>(bool issue, const JsonRpcId& id, Consume&& data);

    void V6Api::onHandleGetAssetInfo(const JsonRpcId &id, GetAssetInfo&& data)
    {
        LOG_DEBUG() << " GetAssetInfo" << "(id = " << id << " asset_id = " << data.assetId << ")";

        auto walletDB = getWalletDB();
        boost::optional<WalletAsset> info = walletDB->findAsset(data.assetId);

        if(!info.is_initialized())
        {
            throw jsonrpc_exception(ApiError::InternalErrorJsonRpc, "Asset not found in a local database. Update asset info (tx_asset_info) and try again.");
        }

        GetAssetInfo::Response resp;
        resp.AssetInfo = *info;

        doResponse(id, resp);
    }

    void V6Api::onHandleSetConfirmationsCount(const JsonRpcId& id, SetConfirmationsCount&& data)
    {
        auto walletDB = getWalletDB();
        walletDB->setCoinConfirmationsOffset(data.count);
        doResponse(id, GetConfirmationsCount::Response{ walletDB->getCoinConfirmationsOffset() });
    }

    void V6Api::onHandleGetConfirmationsCount(const JsonRpcId& id, GetConfirmationsCount&& data)
    {
        auto walletDB = getWalletDB();
        doResponse(id, GetConfirmationsCount::Response{ walletDB->getCoinConfirmationsOffset() });
    }

    void V6Api::onHandleTxAssetInfo(const JsonRpcId& id, TxAssetInfo&& data)
    {
        LOG_DEBUG() << " AssetInfo" << "(id = " << id << " asset_id = " << data.assetId << ")";
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
            params.SetParameter(TxParameterID::AssetID, data.assetId);

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

    void V6Api::onHandleStatus(const JsonRpcId& id, Status&& data)
    {
        LOG_DEBUG() << "Status(txId = " << to_hex(data.txId.data(), data.txId.size()) << ")";

        auto walletDB = getWalletDB();
        auto tx = walletDB->getTx(data.txId);

        if (tx)
        {
            checkTxAccessRights(*tx, ApiError::InvalidParamsJsonRpc, kUnknownTxID);

            Block::SystemState::ID stateID = {};
            walletDB->getSystemStateID(stateID);

            Status::Response result;
            result.tx            = *tx;
            result.systemHeight  = stateID.m_Height;
            result.txProofHeight = storage::DeduceTxProofHeight(*walletDB, *tx);

            doResponse(id, result);
        }
        else
        {
            throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, kUnknownTxID);
        }
    }

    void V6Api::onHandleSplit(const JsonRpcId& id, Split&& data)
    {
        LOG_DEBUG() << "Split(id = " << id << " asset_id " << (data.assetId ? *data.assetId : 0) << " coins = [";
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

    void V6Api::onHandleTxCancel(const JsonRpcId& id, TxCancel&& data)
    {
        LOG_DEBUG() << "TxCancel(txId = " << to_hex(data.txId.data(), data.txId.size()) << ")";

        auto walletDB = getWalletDB();
        auto wallet = getWallet();
        auto tx = walletDB->getTx(data.txId);

        if (tx)
        {
            checkTxAccessRights(*tx, ApiError::InvalidParamsJsonRpc, kUnknownTxID);

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
            throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, kUnknownTxID);
        }
    }

    void V6Api::onHandleTxDelete(const JsonRpcId& id, TxDelete&& data)
    {
        LOG_DEBUG() << "TxDelete(txId = " << to_hex(data.txId.data(), data.txId.size()) << ")";

        auto walletDB = getWalletDB();
        auto tx = walletDB->getTx(data.txId);

        if (tx)
        {
            checkTxAccessRights(*tx, ApiError::InvalidParamsJsonRpc, kUnknownTxID);

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
            throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, kUnknownTxID);
        }
    }

    void V6Api::onHandleGetUtxo(const JsonRpcId& id, GetUtxo&& data)
    {
        LOG_DEBUG() << "GetUtxo(id = " << id << " assets = " << getCAEnabled()
                    << " asset_id = " << (data.filter.assetId ? *data.filter.assetId : 0)
                    << ")";

        auto walletDB = getWalletDB();

        GetUtxo::Response response;
        response.confirmations_count = walletDB->getCoinConfirmationsOffset();

        auto processCoin = [&](const auto& c)->bool
        {
            if (c.isAsset() && !getCAEnabled())
            {
                return true;
            }

            if (data.filter.assetId && !c.isAsset(*data.filter.assetId))
            {
                return true;
            }

            ApiCoin::EmplaceCoin(response.coins, c);
            return true;
        };

        walletDB->visitCoins(processCoin);
        walletDB->visitShieldedCoins(processCoin);

        if (data.sort.field != "default")
        {
            if (const auto& it = utxoSortMap.find(data.sort.field); it != utxoSortMap.end())
            {
                std::sort(response.coins.begin(), response.coins.end(),data.sort.desc ? std::bind(it->second, _2, _1) : it->second);
            }
            else
            {
                throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, "Can't sort by \"" + data.sort.field + "\" field");
            }
        }
        else if (data.sort.desc)
        {
            std::reverse(response.coins.begin(), response.coins.end());
        }

        doPagination(data.skip, data.count, response.coins);
        doResponse(id, response);
    }

    void V6Api::onHandleWalletStatusApi(const JsonRpcId& id, WalletStatusApi&& data)
    {
        LOG_DEBUG() << "WalletStatus(id = " << id << ")";

        WalletStatusApi::Response response;
        auto walletDB = getWalletDB();

        Block::SystemState::ID stateID = {};
        walletDB->getSystemStateID(stateID);
        response.currentHeight = stateID.m_Height;
        response.currentStateHash = stateID.m_Hash;

        {
            Block::SystemState::Full state;
            walletDB->get_History().get_At(state, stateID.m_Height);

            Merkle::Hash stateHash;
            state.get_Hash(stateHash);
            assert(stateID.m_Hash == stateHash);

            response.prevStateHash = state.m_Prev;
            response.difficulty = state.m_PoW.m_Difficulty.ToFloat();
        }

        storage::Totals allTotals(*walletDB, false);
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

    void V6Api::onHandleGenerateTxId(const JsonRpcId& id, GenerateTxId&& data)
    {
        LOG_DEBUG() << "GenerateTxId(id = " << id << ")";
        doResponse(id, GenerateTxId::Response{ wallet::GenerateTxID() });
    }

    bool V6Api::allowedTx(const TxDescription& tx)
    {
        if (!checkTxAccessRights(tx))
        {
           return false;
        }

        if (tx.m_assetId > 0 && !getCAEnabled())
        {
            return false;
        }

        switch(tx.m_txType)
        {
        case TxType::Simple:
        case TxType::PushTransaction:
        case TxType::AssetIssue:
        case TxType::AssetConsume:
        case TxType::AssetInfo:
        case TxType::AssetReg:
        case TxType::AssetUnreg:
        case TxType::Contract:
            return true;
        default:
            return false;
        }
    }

    void V6Api::onHandleTxList(const JsonRpcId& id, TxList&& data)
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
            if (getCAEnabled())
            {
                filter.m_AssetConfirmedHeight = data.filter.height;
            }
            filter.m_KernelProofHeight = data.filter.height;
            walletDB->visitTx([&](const TxDescription& tx)
            {
                if (!allowedTx(tx))
                {
                    return true;
                }

                ++offset;
                if (offset <= data.skip)
                {
                    return true;
                }
                Status::Response& item = res.resultList.emplace_back();
                item.tx = tx;
                item.txProofHeight = storage::DeduceTxProofHeight(*walletDB, tx);
                item.systemHeight = stateID.m_Height;

                ++counter;
                return data.count == 0 || counter < data.count;
            }, filter);
            assert(data.count == 0 || (uint32_t)res.resultList.size() <= data.count);
        }
        
        doResponse(id, res);
        sw.stop();
        LOG_DEBUG() << "TxList  elapsed time: " << sw.milliseconds() << " ms\n";
    }

    void V6Api::onHandleExportPaymentProof(const JsonRpcId& id, ExportPaymentProof&& data)
    {
        LOG_DEBUG() << "ExportPaymentProof(id = " << id << ")";

        auto walletDB = getWalletDB();
        auto tx = walletDB->getTx(data.txId);

        if (!tx)
        {
            throw jsonrpc_exception(ApiError::PaymentProofExportError, kErrorPpExportFailed);
        }

        checkTxAccessRights(*tx, ApiError::PaymentProofExportError, kErrorPpExportFailed);

        if (!tx->m_sender || tx->m_selfTx ||
            tx->m_txType == TxType::AssetIssue ||
            tx->m_txType == TxType::AssetConsume ||
            tx->m_txType == TxType::AssetReg ||
            tx->m_txType == TxType::AssetUnreg ||
            tx->m_txType == TxType::Contract)
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

    void V6Api::onHandleVerifyPaymentProof(const JsonRpcId &id, VerifyPaymentProof&& data)
    {
        LOG_DEBUG() << "VerifyPaymentProof(id = " << id << ")";
        try
        {
            VerifyPaymentProof::Response response;
            try
            {
                response.paymentInfo = storage::PaymentInfo::FromByteBuffer(data.paymentProof);
            }
            catch(...)
            {
                response.shieldedPaymentInfo = storage::ShieldedPaymentInfo::FromByteBuffer(data.paymentProof);
            }
            
            doResponse(id, response);
        }
        catch (...)
        {
            throw jsonrpc_exception(ApiError::InvalidPaymentProof, "Failed to parse");
        }
    }

    void V6Api::onHandleInvokeContract(const JsonRpcId &id, InvokeContract&& data)
    {
        LOG_VERBOSE() << "InvokeContract(id = " << id << ")";
        auto contracts = getContracts();

        if (!contracts->IsDone())
        {
            throw jsonrpc_exception(ApiError::UnexpectedError, "Previous shader call is still in progress");
        }

        if (data.createTx)
        {
            onHandleInvokeContractWithTX(id, std::move(data));
        }
        else
        {
            onHandleInvokeContractNoTX(id, std::move(data));
        }
    }

    void V6Api::onHandleInvokeContractWithTX(const JsonRpcId &id, InvokeContract&& data)
    {
        getContracts()->CallShaderAndStartTx(data.contract, data.args, data.args.empty() ? 0 : 1, 0, 0,
        [this, id, wguard = _weakSelf](boost::optional<TxID> txid, boost::optional<std::string> result, boost::optional<std::string> error) {
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

            InvokeContract::Response response;
            response.output = result ? *result : "";
            response.txid   = txid ? *txid : TxID();

            doResponse(id, response);
        });
    }

    void V6Api::onHandleInvokeContractNoTX(const JsonRpcId &id, InvokeContract&& data)
    {
        getContracts()->CallShader(data.contract, data.args, data.args.empty() ? 0 : 1, 0, 0,
        [this, id, wguard = _weakSelf](boost::optional<ByteBuffer> data, boost::optional<std::string> output, boost::optional<std::string> error) {
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

            InvokeContract::Response response;
            response.output = std::move(output);
            response.invokeData = std::move(data);

            doResponse(id, response);
        });
    }

    void V6Api::onHandleProcessInvokeData(const JsonRpcId &id, ProcessInvokeData&& data)
    {
        LOG_DEBUG() << "ProcessInvokeData(id = " << id << ")";
        getContracts()->ProcessTxData(data.invokeData,
        [this, id, wguard = _weakSelf](boost::optional<TxID> txid, boost::optional<std::string> error) {
            auto guard = wguard.lock();
            if (!guard)
            {
                LOG_WARNING() << "API destroyed before shader response received.";
                return;
            }

            if (error)
            {
                return sendError(id, ApiError::ContractError, *error);
            }

            if (!txid)
            {
                return sendError(id, ApiError::ContractError, "Missing txid");
            }

            ProcessInvokeData::Response response;
            response.txid = *txid;

            doResponse(id, response);
        });
    }

    void V6Api::onHandleBlockDetails(const JsonRpcId& id, BlockDetails&& data)
    {
        LOG_DEBUG() << "BlockDetails(id = " << id << ")";

        RequestHeaderMsg::Ptr request(new RequestHeaderMsg(id, _weakSelf, *this));
        request->m_Msg.m_Height = data.blockHeight;

        getWallet()->GetNodeEndpoint()->PostRequest(*request, *request);
    }

    void V6Api::RequestHeaderMsg::OnComplete(proto::FlyClient::Request& request)
    {
        auto guard = _guard.lock();
        if (!guard)
        {
            LOG_WARNING() << "API destroyed before fly client response received.";
            return;
        }

        auto headerRequest = dynamic_cast<RequestHeaderMsg&>(request);
        headerRequest.m_pTrg = nullptr;

        if (headerRequest.m_vStates.size() != 1)
        {
            _wapi.sendError(headerRequest._id, ApiError::InternalErrorJsonRpc, "Cannot get block header.");
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

        _wapi.doResponse(headerRequest._id, response);
    }
}
