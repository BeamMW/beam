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

    std::map<std::string, std::function<bool(const GetUtxo::Response::Coin& a, const GetUtxo::Response::Coin& b)>> utxoSortMap = 
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
    void WalletApi::FillAddressData(const AddressData& data, WalletAddress& address)
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

    void WalletApi::onHandleCalcChange(const JsonRpcId& id, const CalcChange& data)
    {
        LOG_DEBUG() << "CalcChange(id = " << id << " amount = " << data.amount << " asset_id = " << (data.assetId ? *data.assetId : 0) << ")";

        CoinsSelectionInfo csi = {0};

        csi.m_requestedSum = data.amount;
        if (data.assetId)
        {
            csi.m_assetID = *data.assetId;
        }

        csi.m_explicitFee = data.explicitFee;
        csi.Calculate(get_CurrentHeight(), getWalletDB(), data.isPushTransaction);

        doResponse(id, CalcChange::Response{ csi.m_changeBeam, csi.m_changeAsset, csi.m_explicitFee });
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

        if (_appId.empty())
        {
            return doResponse(id, AddrList::Response{all});
        }

        decltype(all) filtered;
        for(const auto& addr: all)
        {
            if (_appId.empty() || addr.m_category == _appId)
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

        if (data.comment)    address.setLabel(*data.comment);
        if (data.expiration) address.setExpirationStatus(*data.expiration);
        if (!_appId.empty()) address.setCategory(_appId);

        address.m_Address = GenerateToken(data.type, address,  walletDB, data.offlinePayments);
        walletDB->saveAddress(address);

        doResponse(id, CreateAddress::Response{address.m_Address});
    }

    void WalletApi::onHandleDeleteAddress(const JsonRpcId& id, const DeleteAddress& data)
    {
        LOG_DEBUG() << "DeleteAddress(id = " << id << " address = " << data.token << ")";

        auto walletDB = getWalletDB();
        auto addr = walletDB->getAddressByToken(data.token);

        if (addr)
        {
            if (!_appId.empty() && addr->m_category != _appId)
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

    void WalletApi::onHandleEditAddress(const JsonRpcId& id, const EditAddress& data)
    {
        LOG_DEBUG() << "EditAddress(id = " << id << " address = " << data.token << ")";

        auto walletDB = getWalletDB();
        auto addr = walletDB->getAddressByToken(data.token);

        if (addr)
        {
            if (!_appId.empty() && addr->m_category != _appId)
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

    void WalletApi::onHandleValidateAddress(const JsonRpcId& id, const ValidateAddress& data)
    {
        LOG_DEBUG() << "ValidateAddress( address = " << data.token << ")";


        auto tokenType = GetTokenType(data.token);

        auto p = ParseParameters(data.token);

        bool isValid = !!p;
        bool isMine = false;
        boost::optional<size_t> offlinePayments;

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
                    if (tokenType == TokenType::Offline)
                    {
                        if (auto vouchers = p->GetParameter<ShieldedVoucherList>(TxParameterID::ShieldedVoucherList); vouchers)
                        {
                            storage::SaveVouchers(*walletDB, *vouchers, *v);
                        }
                        offlinePayments = walletDB->getVoucherCount(*v);
                    }
                }
            }
        }

        doResponse(id, ValidateAddress::Response{ isValid, isMine, tokenType, offlinePayments });
    }

    void WalletApi::doTxAlreadyExistsError(const JsonRpcId& id)
    {
        throw jsonrpc_exception(ApiError::InvalidTxId, "Provided transaction ID already exists in the wallet.");
    }

    void WalletApi::onHandleSend(const JsonRpcId& id, const Send& data)
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

            if (!_appId.empty())
            {
                params.SetParameter(TxParameterID::AppID, _appId);
            }

            if (!_appName.empty())
            {
                params.SetParameter(TxParameterID::AppName, _appName);
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
    void WalletApi::setTxAssetParams(const JsonRpcId& id, TxParameters& params, const T& data)
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

    template void WalletApi::setTxAssetParams(const JsonRpcId& id, TxParameters& params, const Issue& data);
    template void WalletApi::setTxAssetParams(const JsonRpcId& id, TxParameters& params, const Consume& data);

    void WalletApi::onHandleRegister(const JsonRpcId &id, const Register &data)
    {
        WalletApi::onHandleRegisterUnregister(false, id, data);
    }

    void WalletApi::onHandleUnregister(const JsonRpcId &id, const Unregister &data)
    {
        WalletApi::onHandleRegisterUnregister(false, id, data);
    }

    template <typename T>
    void WalletApi::onHandleRegisterUnregister(bool doRegister, const JsonRpcId& id, const T& data)
    {
        try {
            auto walletDB = getWalletDB();
            auto wallet = getWallet();
            CoinIDList coins = data.coins ? *data.coins : CoinIDList();

            WalletAssetMeta meta(data.asset_meta);

            if (data.txId && walletDB->getTx(*data.txId)) {
                doTxAlreadyExistsError(id);
                return;
            }

            auto params = CreateTransactionParameters(doRegister ? TxType::AssetReg : TxType::AssetUnreg)
                    .SetParameter(TxParameterID::Amount, Rules::get().CA.DepositForList)
                    .SetParameter(TxParameterID::Fee, data.fee)
                    .SetParameter(TxParameterID::PreselectedCoins, coins)
                    .SetParameter(TxParameterID::AssetMetadata, data.asset_meta);

            const auto txId = wallet->StartTransaction(params);
            doResponse(id, Register::Response{txId});
        } catch(const jsonrpc_exception&) {
            throw;
        }
        catch (...)
        {
            throw jsonrpc_exception(ApiError::InternalErrorJsonRpc, "Transaction could not be created. Please look at logs.");
        }
    }

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

    template void WalletApi::onHandleIssueConsume<Issue>(bool issue, const JsonRpcId& id, const Issue& data);
    template void WalletApi::onHandleIssueConsume<Consume>(bool issue, const JsonRpcId& id, const Consume& data);

    void WalletApi::onHandleGetAssetInfo(const JsonRpcId &id, const GetAssetInfo &data)
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

    void WalletApi::onHandleStatus(const JsonRpcId& id, const Status& data)
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
            result.confirmations = 0;
            result.txHeight      = storage::DeduceTxProofHeight(*walletDB, *tx);

            doResponse(id, result);
        }
        else
        {
            throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, kUnknownTxID);
        }
    }

    void WalletApi::onHandleSplit(const JsonRpcId& id, const Split& data)
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

    void WalletApi::onHandleTxCancel(const JsonRpcId& id, const TxCancel& data)
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

    void WalletApi::onHandleTxDelete(const JsonRpcId& id, const TxDelete& data)
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

    void WalletApi::onHandleGetUtxo(const JsonRpcId& id, const GetUtxo& data)
    {
        LOG_DEBUG() << "GetUtxo(id = " << id << " assets = " << data.withAssets
                    << " asset_id = " << (data.filter.assetId ? *data.filter.assetId : 0)
                    << ")";

        auto walletDB = getWalletDB();

        GetUtxo::Response response;
        response.confirmations_count = walletDB->getCoinConfirmationsOffset();

        auto processCoin = [&](const auto& c)->bool 
        {
            if (!data.withAssets && c.isAsset())
            {
                return true;
            }
            if (data.filter.assetId && !c.isAsset(*data.filter.assetId))
            {
                return true;
            }
            response.EmplaceCoin(c);
            return true;
        };

        walletDB->visitCoins(processCoin);

        walletDB->visitShieldedCoins(processCoin);

        if (data.sort.field != "default")
        {
            if (const auto& it = utxoSortMap.find(data.sort.field); it != utxoSortMap.end())
            {
                std::sort(response.coins.begin(), response.coins.end(),
                        data.sort.desc ? std::bind(it->second, _2, _1) : it->second);
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

                if (!checkTxAccessRights(tx))
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

        checkTxAccessRights(*tx, ApiError::PaymentProofExportError, kErrorPpExportFailed);

        if (!tx->m_sender || tx->m_selfTx)
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

        if (data.createTx)
        {
            onHandleInvokeContractWithTX(id, data);
        }
        else
        {
            onHandleInvokeContractNoTX(id, data);
        }
    }

    void WalletApi::onHandleInvokeContractWithTX(const JsonRpcId &id, const InvokeContract& data)
    {
        std::weak_ptr<bool> wguard = _contractsGuard;
        auto contracts = getContracts();

        contracts->CallShaderAndStartTx(data.args, data.args.empty() ? 0 : 1, [this, id, wguard](boost::optional<TxID> txid, boost::optional<std::string> result, boost::optional<std::string> error) {
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

    void WalletApi::onHandleInvokeContractNoTX(const JsonRpcId &id, const InvokeContract& data)
    {
        std::weak_ptr<bool> wguard = _contractsGuard;
        auto contracts = getContracts();

        contracts->CallShader(data.args, data.args.empty() ? 0 : 1, [this, id, wguard](boost::optional<ByteBuffer> data, boost::optional<std::string> output, boost::optional<std::string> error) {
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

    void WalletApi::onHandleProcessInvokeData(const JsonRpcId &id, const ProcessInvokeData &data)
    {
        LOG_DEBUG() << "ProcessInvokeData(id = " << id << ")";

        auto contracts = getContracts();
        std::weak_ptr<bool> wguard = _contractsGuard;

        contracts->ProcessTxData(data.invokeData, [this, id, wguard](boost::optional<TxID> txid, boost::optional<std::string> error) {
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

    void WalletApi::onHandleBlockDetails(const JsonRpcId& id, const BlockDetails& data)
    {
        LOG_DEBUG() << "BlockDetails(id = " << id << ")";

        RequestHeaderMsg::Ptr request(new RequestHeaderMsg(id, _contractsGuard, *this));
        request->m_Msg.m_Height = data.blockHeight;

        getWallet()->GetNodeEndpoint()->PostRequest(*request, *request);
    }

    void WalletApi::RequestHeaderMsg::OnComplete(proto::FlyClient::Request& request)
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
