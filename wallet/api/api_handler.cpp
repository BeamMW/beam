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

#include "api_handler.h"
#include "wallet/core/simple_transaction.h"
#include "wallet/core/strings_resources.h"
#include "wallet/transactions/assets/assets_kdf_utils.h"
#include "utility/logger.h"

#ifdef BEAM_ATOMIC_SWAP_SUPPORT
#include "wallet/transactions/swaps/utils.h"
#include <regex>
#endif  // BEAM_ATOMIC_SWAP_SUPPORT

using namespace beam;

namespace
{
#ifdef BEAM_ATOMIC_SWAP_SUPPORT
    using namespace beam::wallet;

    const char kSwapAmountToLowError[] = "The swap amount must be greater than the redemption fee.";
    const char kBeamAmountToLowError[] = "\'beam_amount\' must be greater than \"beam_fee\".";
    const char kSwapNotEnoughtSwapCoins[] = "There is not enough funds to complete the transaction.";
    const char kSwapFeeToLowRecommenededError[] = "\'fee_rate\' must be greater or equal than recommended fee rate.";
    const char kSwapFeeToLowError[] = "\'fee_rate\' must be greater or equal than ";

    void checkIsEnoughtBeamAmount(IWalletDB::Ptr walletDB, Amount beamAmount, Amount beamFee)
    {
        storage::Totals allTotals(*walletDB);
        const auto& totals = allTotals.GetBeamTotals();
        const auto available = AmountBig::get_Lo(totals.Avail);
        if (beamAmount + beamFee > available)
        {
            throw NotEnoughtBeams();
        }
    }

    bool checkIsEnoughtSwapAmount(
        const IAtomicSwapProvider& swapProvider, AtomicSwapCoin swapCoin,
        Amount swapAmount, Amount swapFeeRate)
    {
        beam::Amount total = swapAmount + swapFeeRate;
        return swapProvider.getCoinAvailable(swapCoin) > total;
    }

    void checkSwapConnection(const IAtomicSwapProvider& swapProvider, AtomicSwapCoin swapCoin)
    {
        if (!swapProvider.isCoinClientConnected(swapCoin))
        {
            throw FailToConnectSwap(std::to_string(swapCoin));
        }
    }

    boost::optional<SwapOffer> getOfferFromBoardByTxId(
        const std::vector<SwapOffer>& board, const TxID& txId)
    {
        auto it = std::find_if(
            board.begin(), board.end(),
            [txId](const SwapOffer& publicOffer)
            {
                auto publicTxId = publicOffer.GetTxID();
                if (publicTxId)
                    return txId == *publicTxId;
                return false;
            });
        if (it != board.end())
            return *it;

        return boost::optional<SwapOffer>();
    }

    WalletID createWID(IWalletDB* walletDb, const std::string& comment)
    {
        WalletAddress address;
        walletDb->createAddress(address);
        if (!comment.empty())
            address.m_label = comment;
        address.m_duration = WalletAddress::AddressExpiration24h;
        walletDb->saveAddress(address);

        return address.m_walletID;
    }

    bool checkAcceptableTxParams(const TxParameters& params, const OfferInput& data)
    {
        auto beamAmount = params.GetParameter<Amount>(TxParameterID::Amount);
        if (!beamAmount || *beamAmount != data.beamAmount)
            return false;

        auto swapAmount = params.GetParameter<Amount>(
            TxParameterID::AtomicSwapAmount);
        if (!swapAmount || *swapAmount != data.swapAmount)
            return false;

        auto swapCoin = params.GetParameter<AtomicSwapCoin>(
            TxParameterID::AtomicSwapCoin);
        if (!swapCoin || *swapCoin != data.swapCoin)
            return false;

        auto isBeamSide = params.GetParameter<bool>(
            TxParameterID::AtomicSwapIsBeamSide);
        if (!isBeamSide || *isBeamSide != data.isBeamSide)
            return false;

        return true;
    }

    // TODO roman.strilets: it's duplicate of checkAcceptableTxParams. should be refactored
    bool checkPublicOffer(const TxParameters& params, const SwapOffer& publicOffer)
    {
        auto beamAmount = params.GetParameter<Amount>(TxParameterID::Amount);
        if (!beamAmount || *beamAmount != publicOffer.amountBeam())
            return false;

        auto swapAmount = params.GetParameter<Amount>(
            TxParameterID::AtomicSwapAmount);
        if (!swapAmount || *swapAmount != publicOffer.amountSwapCoin())
            return false;

        auto swapCoin = params.GetParameter<AtomicSwapCoin>(
            TxParameterID::AtomicSwapCoin);
        if (!swapCoin || *swapCoin != publicOffer.swapCoinType())
            return false;

        auto isBeamSide = params.GetParameter<bool>(
            TxParameterID::AtomicSwapIsBeamSide);
        if (!isBeamSide || *isBeamSide != publicOffer.isBeamSide())
            return false;

        return true;
    }

#endif  // BEAM_ATOMIC_SWAP_SUPPORT

}  // namespace

namespace beam::wallet
{
    WalletApiHandler::WalletApiHandler(
        IWalletData& walletData
        , WalletApi::ACL acl)
        : _walletData(walletData)
        , _api(*this, acl)
    {
    }

    WalletApiHandler::~WalletApiHandler()
    {
    }

    void WalletApiHandler::doError(const JsonRpcId& id, ApiError code, const std::string& data)
    {
        json msg
        {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"error",
                {
                    {"code", code},
                    {"message", WalletApi::getErrorMessage(code)}
                }
            }
        };

        if (!data.empty())
        {
            msg["error"]["data"] = data;
        }

        serializeMsg(msg);
    }

    void WalletApiHandler::onInvalidJsonRpc(const json& msg)
    {
        LOG_DEBUG() << "onInvalidJsonRpc: " << msg;

        serializeMsg(msg);
    }

    void WalletApiHandler::FillAddressData(const AddressData& data, WalletAddress& address)
    {
        if (data.comment)
        {
            address.setLabel(*data.comment);
        }

        if (data.expiration)
        {
            switch (*data.expiration)
            {
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

    void WalletApiHandler::onMessage(const JsonRpcId& id, const CreateAddress& data)
    {
        LOG_DEBUG() << "CreateAddress(id = " << id << ")";

        WalletAddress address;
        auto walletDB = _walletData.getWalletDBPtr();

        if (!walletDB) {
            return doError(id, ApiError::NotOpenedError);
        }

        walletDB->createAddress(address);
        FillAddressData(data, address);

        walletDB->saveAddress(address);
        doResponse(id, CreateAddress::Response{ address.m_walletID });
    }

    void WalletApiHandler::onMessage(const JsonRpcId& id, const DeleteAddress& data)
    {
        LOG_DEBUG() << "DeleteAddress(id = " << id << " address = " << std::to_string(data.address) << ")";

        auto walletDB = _walletData.getWalletDBPtr();
        if (!walletDB) {
            return doError(id, ApiError::NotOpenedError);
        }

        auto addr = walletDB->getAddress(data.address);

        if (addr)
        {
            walletDB->deleteAddress(data.address);

            doResponse(id, DeleteAddress::Response{});
        }
        else
        {
            doError(id, ApiError::InvalidAddress, "Provided address doesn't exist.");
        }
    }

    void WalletApiHandler::onMessage(const JsonRpcId& id, const EditAddress& data)
    {
        LOG_DEBUG() << "EditAddress(id = " << id << " address = " << std::to_string(data.address) << ")";

        auto walletDB = _walletData.getWalletDBPtr();
        if (!walletDB) {
            return doError(id, ApiError::NotOpenedError);
        }

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
                doError(id, ApiError::InvalidAddress, "You can edit only own address.");
            }
        }
        else
        {
            doError(id, ApiError::InvalidAddress, "Provided address doesn't exist.");
        }
    }

    void WalletApiHandler::onMessage(const JsonRpcId& id, const AddrList& data)
    {
        LOG_DEBUG() << "AddrList(id = " << id << ")";

        auto walletDB = _walletData.getWalletDBPtr();
        if (!walletDB) {
            return doError(id, ApiError::NotOpenedError);
        }

        doResponse(id, AddrList::Response{walletDB->getAddresses(data.own)});
    }

    void WalletApiHandler::onMessage(const JsonRpcId& id, const ValidateAddress& data)
    {
        LOG_DEBUG() << "ValidateAddress( address = " << std::to_string(data.address) << ")";

        auto walletDB = _walletData.getWalletDBPtr();
        if (!walletDB) {
            return doError(id, ApiError::NotOpenedError);
        }

        bool isValid = data.address.IsValid();
        bool isMine = false;

        auto addr = walletDB->getAddress(data.address);
        if (addr)
        {
            isMine = addr->isOwn();
            if (isMine)
            {
                isValid = isValid && !addr->isExpired();
            }
        }
        doResponse(id, ValidateAddress::Response{ isValid, isMine });
    }

    void WalletApiHandler::doTxAlreadyExistsError(const JsonRpcId& id)
    {
        doError(id, ApiError::InvalidTxId, "Provided transaction ID already exists in the wallet.");
    }

    void WalletApiHandler::onMessage(const JsonRpcId& id, const Send& data)
    {
        LOG_DEBUG() << "Send(id = " << id
                    << " asset_id = " << (data.assetId ? *data.assetId : 0)
                    << " amount = " << data.value << " fee = " << data.fee << " address = " << std::to_string(data.address) << ")";

        try
        {
            WalletID from(Zero);

            auto walletDB = _walletData.getWalletDBPtr();
            auto wallet = _walletData.getWalletPtr();

            if (!walletDB || !wallet) {
                return doError(id, ApiError::NotOpenedError);
            }

            if (data.from)
            {
                if (!data.from->IsValid())
                {
                    doError(id, ApiError::InvalidAddress, "Invalid sender address.");
                    return;
                }

                auto addr = walletDB->getAddress(*data.from);
                if (!addr || !addr->isOwn())
                {
                    doError(id, ApiError::InvalidAddress, "It's not your own address.");
                    return;
                }

                if (addr->isExpired())
                {
                    doError(id, ApiError::InvalidAddress, "Sender address is expired.");
                    return;
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
                    doError(id, ApiError::InternalErrorJsonRpc, "Requested session is empty.");
                    return;
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
            LoadReceiverParams(data.txParameters, params);

            if (auto token = data.txParameters.GetParameter<std::string>(beam::wallet::TxParameterID::OriginalToken); token)
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
        catch (...)
        {
            doError(id, ApiError::InternalErrorJsonRpc, "Transaction could not be created. Please look at logs.");
        }
    }

    template<typename T>
    bool WalletApiHandler::setTxAssetParams(const JsonRpcId& id, TxParameters& params, const T& data)
    {
        auto walletDB = _walletData.getWalletDBPtr();
        if (!walletDB) {
            doError(id, ApiError::NotOpenedError);
            return false;
        }

        if (data.assetMeta)
        {
            params.SetParameter(TxParameterID::AssetMetadata, *data.assetMeta);
            return true;
        }
        else if (data.assetId)
        {
            if (const auto asset = walletDB->findAsset(*data.assetId))
            {
                std::string strmeta;
                if(!fromByteBuffer(asset->m_Metadata.m_Value, strmeta))
                {
                    doError(id, ApiError::InternalErrorJsonRpc, "Failed to load asset metadata.");
                    return false;
                }

                params.SetParameter(TxParameterID::AssetMetadata, strmeta);
                return true;
            }
            else
            {
                doError(id, ApiError::InternalErrorJsonRpc, "Asset not found in a local database. Update asset info (tx_asset_info) or provide asset metdata.");
                return false;
            }
        }

        assert(!"presence of params should be checked already");
        doError(id, ApiError::InternalErrorJsonRpc, "asset_id or meta is required");
        return false;
    }

    template bool WalletApiHandler::setTxAssetParams(const JsonRpcId& id, TxParameters& params, const Issue& data);
    template bool WalletApiHandler::setTxAssetParams(const JsonRpcId& id, TxParameters& params, const Consume& data);

    void WalletApiHandler::onMessage(const JsonRpcId& id, const Issue& data)
    {
        onIssueConsumeMessage(true, id, data);
    }

    void WalletApiHandler::onMessage(const JsonRpcId& id, const Consume& data)
    {
        onIssueConsumeMessage(false, id, data);
    }

    template<typename T>
    void WalletApiHandler::onIssueConsumeMessage(bool issue, const JsonRpcId& id, const T& data)
    {
        LOG_DEBUG() << (issue ? " Issue" : " Consume") << "(id = " << id << " asset_id = "
                    << (data.assetId ? *data.assetId : 0)
                    << " asset_meta = " << (data.assetMeta ? *data.assetMeta : "\"\"")
                    << " amount = " << data.value << " fee = " << data.fee
                    << ")";
        try
        {
            CoinIDList coins;
            auto walletDB = _walletData.getWalletDBPtr();
            auto wallet = _walletData.getWalletPtr();
            if (!walletDB || !wallet) {
                return doError(id, ApiError::NotOpenedError);
            }

            if (data.session)
            {
                if ((coins = walletDB->getLockedCoins(*data.session)).empty())
                {
                    doError(id, ApiError::InternalErrorJsonRpc, "Requested session is empty.");
                    return;
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

            if(!setTxAssetParams(id, params, data))
            {
                return;
            }

            const auto txId = wallet->StartTransaction(params);
            doResponse(id, Issue::Response{ txId });
        }
        catch (...)
        {
            doError(id, ApiError::InternalErrorJsonRpc, "Transaction could not be created. Please look at logs.");
        }
    }

    template void WalletApiHandler::onIssueConsumeMessage<Issue>(bool issue, const JsonRpcId& id, const Issue& data);
    template void WalletApiHandler::onIssueConsumeMessage<Consume>(bool issue, const JsonRpcId& id, const Consume& data);

    void WalletApiHandler::onMessage(const JsonRpcId& id, const GetAssetInfo& data)
    {
        LOG_DEBUG() << " GetAssetInfo" << "(id = " << id << " asset_id = "
                    << (data.assetId ? *data.assetId : 0)
                    << " asset_meta = " << (data.assetMeta ? *data.assetMeta : "\"\"")
                    << ")";

        auto walletDB = _walletData.getWalletDBPtr();
        if (!walletDB) {
            return doError(id, ApiError::NotOpenedError);
        }

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
            doError(id, ApiError::InternalErrorJsonRpc, "Asset not found in a local database. Update asset info (tx_asset_info) and try again.");
            return;
        }

        GetAssetInfo::Response resp;
        resp.AssetInfo = *info;

        doResponse(id, resp);
    }

    void WalletApiHandler::onMessage(const JsonRpcId& id, const SetConfirmationsCount& data)
    {
        auto walletDB = _walletData.getWalletDBPtr();
        walletDB->setCoinConfirmationsOffset(data.count);
        doResponse(id, GetConfirmationsCount::Response{ walletDB->getCoinConfirmationsOffset() });
    }

    void WalletApiHandler::onMessage(const JsonRpcId& id, const GetConfirmationsCount& data)
    {
        auto walletDB = _walletData.getWalletDBPtr();
        doResponse(id, GetConfirmationsCount::Response{ walletDB->getCoinConfirmationsOffset() });
    }

    void WalletApiHandler::onMessage(const JsonRpcId& id, const TxAssetInfo& data)
    {
        LOG_DEBUG() << " AssetInfo" << "(id = " << id << " asset_id = "
                    << (data.assetId ? *data.assetId : 0)
                    << " asset_meta = " << (data.assetMeta ? *data.assetMeta : "\"\"")
                    << ")";
        try
        {
            auto walletDB = _walletData.getWalletDBPtr();
            auto wallet = _walletData.getWalletPtr();
            if (!walletDB || !wallet) {
                return doError(id, ApiError::NotOpenedError);
            }

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
                doError(id, ApiError::InternalErrorJsonRpc, "asset_id or meta is required");
                return;
            }

            const auto txId = wallet->StartTransaction(params);
            doResponse(id, Issue::Response{ txId });
        }
        catch (...)
        {
            doError(id, ApiError::InternalErrorJsonRpc, "Transaction could not be created. Please look at logs.");
        }
    }

    void WalletApiHandler::onMessage(const JsonRpcId& id, const Status& data)
    {
        LOG_DEBUG() << "Status(txId = " << to_hex(data.txId.data(), data.txId.size()) << ")";

        auto walletDB = _walletData.getWalletDBPtr();
        if (!walletDB) {
            return doError(id, ApiError::NotOpenedError);
        }

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
            doError(id, ApiError::InvalidParamsJsonRpc, "Unknown transaction ID.");
        }
    }

    void WalletApiHandler::onMessage(const JsonRpcId& id, const Split& data)
    {
        LOG_DEBUG() << "Split(id = " << id
                    << " asset_id " << (data.assetId ? *data.assetId : 0)
                    << " coins = [";
        for (auto& coin : data.coins) LOG_DEBUG() << coin << ",";
        LOG_DEBUG() << "], fee = " << data.fee;
        try
        {
            auto walletDB = _walletData.getWalletDBPtr();
            auto wallet = _walletData.getWalletPtr();
            if (!walletDB) {
                return doError(id, ApiError::NotOpenedError);
            }

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
        catch (...)
        {
            doError(id, ApiError::InternalErrorJsonRpc, "Transaction could not be created. Please look at logs.");
        }
    }

    void WalletApiHandler::onMessage(const JsonRpcId& id, const TxCancel& data)
    {
        LOG_DEBUG() << "TxCancel(txId = " << to_hex(data.txId.data(), data.txId.size()) << ")";

        auto walletDB = _walletData.getWalletDBPtr();
        auto wallet = _walletData.getWalletPtr();
        if (!walletDB || !wallet) {
            return doError(id, ApiError::NotOpenedError);
        }

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
                doError(id, ApiError::InvalidTxStatus, "Transaction could not be cancelled. Invalid transaction status.");
            }
        }
        else
        {
            doError(id, ApiError::InvalidParamsJsonRpc, "Unknown transaction ID.");
        }
    }

    void WalletApiHandler::onMessage(const JsonRpcId& id, const TxDelete& data)
    {
        LOG_DEBUG() << "TxDelete(txId = " << to_hex(data.txId.data(), data.txId.size()) << ")";

        auto walletDB = _walletData.getWalletDBPtr();
        if (!walletDB) {
            return doError(id, ApiError::NotOpenedError);
        }

        auto tx = walletDB->getTx(data.txId);

        if (tx)
        {
            if (tx->canDelete())
            {
                walletDB->deleteTx(data.txId);

                if (walletDB->getTx(data.txId))
                {
                    doError(id, ApiError::InternalErrorJsonRpc, "Transaction not deleted.");
                }
                else
                {
                    doResponse(id, TxDelete::Response{ true });
                }
            }
            else
            {
                doError(id, ApiError::InternalErrorJsonRpc, "Transaction can't be deleted.");
            }
        }
        else
        {
            doError(id, ApiError::InvalidParamsJsonRpc, "Unknown transaction ID.");
        }
    }

    void WalletApiHandler::onMessage(const JsonRpcId& id, const GetUtxo& data)
    {
        LOG_DEBUG() << "GetUtxo(id = " << id << " assets = " << data.withAssets
                    << " asset_id = " << (data.filter.assetId ? *data.filter.assetId : 0)
                    << ")";

        auto walletDB = _walletData.getWalletDBPtr();
        if (!walletDB) {
            return doError(id, ApiError::NotOpenedError);
        }

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

        doPagination(data.skip, data.count, response.utxos);
        doResponse(id, response);
    }

    void WalletApiHandler::onMessage(const JsonRpcId& id, const WalletStatus& data)
    {
        LOG_DEBUG() << "WalletStatus(id = " << id << ")";

        WalletStatus::Response response;
        auto walletDB = _walletData.getWalletDBPtr();
        if (!walletDB) {
            return doError(id, ApiError::NotOpenedError);
        }

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

        response.available = AmountBig::get_Lo(totals.Avail);
        response.receiving = AmountBig::get_Lo(totals.Incoming);
        response.sending   = AmountBig::get_Lo(totals.Outgoing);
        response.maturing  = AmountBig::get_Lo(totals.Maturing);

        if (data.withAssets)
        {
            response.totals = allTotals;
        }

        doResponse(id, response);
    }

    void WalletApiHandler::onMessage(const JsonRpcId& id, const GenerateTxId& data)
    {
        LOG_DEBUG() << "GenerateTxId(id = " << id << ")";

        doResponse(id, GenerateTxId::Response{ wallet::GenerateTxID() });
    }

    void WalletApiHandler::onMessage(const JsonRpcId& id, const Lock& data)
    {
        LOG_DEBUG() << "Lock(id = " << id << ")";

        Lock::Response response;

        auto walletDB = _walletData.getWalletDBPtr();
        if (!walletDB) {
            return doError(id, ApiError::NotOpenedError);
        }

        response.result = walletDB->lockCoins(data.coins, data.session);

        doResponse(id, response);
    }

    void WalletApiHandler::onMessage(const JsonRpcId& id, const Unlock& data)
    {
        LOG_DEBUG() << "Unlock(id = " << id << " session = " << data.session << ")";

        Unlock::Response response;

        auto walletDB = _walletData.getWalletDBPtr();
        if (!walletDB) {
            return doError(id, ApiError::NotOpenedError);
        }

        response.result = walletDB->unlockCoins(data.session);

        doResponse(id, response);
    }

    void WalletApiHandler::onMessage(const JsonRpcId& id, const TxList& data)
    {
        LOG_DEBUG() << "List(filter.status = " << (data.filter.status ? std::to_string((uint32_t)*data.filter.status) : "nul") << ")";

        TxList::Response res;

        {
            auto walletDB = _walletData.getWalletDBPtr();
            if (!walletDB) {
                return doError(id, ApiError::NotOpenedError);
            }

            auto txList = walletDB->getTxHistory(TxType::Simple);

            if (data.withAssets)
            {
                auto txIssue = walletDB->getTxHistory(TxType::AssetIssue);
                auto txConsume = walletDB->getTxHistory(TxType::AssetConsume);
                auto txInfo = walletDB->getTxHistory(TxType::AssetInfo);

                txList.insert(txList.end(), txIssue.begin(), txIssue.end());
                txList.insert(txList.end(), txConsume.begin(), txConsume.end());
                txList.insert(txList.end(), txInfo.begin(), txInfo.end());
            }

            std::sort(txList.begin(), txList.end(), [](const TxDescription& a, const TxDescription& b) -> bool {
                return a.m_minHeight > b.m_minHeight;
             });

            Block::SystemState::ID stateID = {};
            walletDB->getSystemStateID(stateID);

            for (const auto& tx : txList)
            {
                if (!data.withAssets && tx.m_assetId != Asset::s_InvalidID)
                {
                    continue;
                }

                if (data.filter.assetId && tx.m_assetId != *data.filter.assetId)
                {
                    continue;
                }

                if (data.filter.status && tx.m_status != *data.filter.status)
                {
                    continue;
                }

                const auto height = storage::DeduceTxProofHeight(*walletDB, tx);
                if (data.filter.height && height != *data.filter.height)
                {
                    continue;
                }

                Status::Response item;
                item.tx = tx;
                item.txHeight = height;
                item.systemHeight = stateID.m_Height;
                item.confirmations = 0;
                res.resultList.push_back(item);
            }
        }

        doPagination(data.skip, data.count, res.resultList);
        doResponse(id, res);
    }

    void WalletApiHandler::onMessage(const JsonRpcId& id, const ExportPaymentProof& data)
    {
        LOG_DEBUG() << "ExportPaymentProof(id = " << id << ")";

        auto walletDB = _walletData.getWalletDBPtr();
        if (!walletDB) {
            return doError(id, ApiError::NotOpenedError);
        }

        auto tx = walletDB->getTx(data.txId);
        if (!tx)
        {
            doError(id, ApiError::PaymentProofExportError, kErrorPpExportFailed);
        }
        else if (!tx->m_sender || tx->m_selfTx)
        {
            doError(id, ApiError::PaymentProofExportError, kErrorPpCannotExportForReceiver);
        }
        else if (tx->m_status != TxStatus::Completed)
        {
            doError(id, ApiError::PaymentProofExportError, kErrorPpExportFailedTxNotCompleted);
        }
        else
        {
            doResponse(id, ExportPaymentProof::Response{ wallet::storage::ExportPaymentProof(*walletDB, data.txId) });
        }
    }

    void WalletApiHandler::onMessage(const JsonRpcId& id, const VerifyPaymentProof& data)
    {
        LOG_DEBUG() << "VerifyPaymentProof(id = " << id << ")";
        try
        {
            doResponse(id, VerifyPaymentProof::Response{ storage::PaymentInfo::FromByteBuffer(data.paymentProof) });
        }
        catch (...)
        {

        }
        doError(id, ApiError::InvalidPaymentProof, "Failed to parse");
    }

#ifdef BEAM_ATOMIC_SWAP_SUPPORT
    void WalletApiHandler::onMessage(const JsonRpcId& id, const OffersList& data)
    {
        std::vector<SwapOffer> publicOffers = _walletData.getAtomicSwapProvider().getSwapOffersBoard().getOffersList();
        auto walletDB = _walletData.getWalletDBPtr();
        if (!walletDB) {
            return doError(id, ApiError::NotOpenedError);
        }

        auto swapTxs = walletDB->getTxHistory(TxType::AtomicSwap);
        std::vector<SwapOffer> offers;

        offers.reserve(swapTxs.size());

        for (const auto& tx : swapTxs)
        {
            SwapOffer offer(tx);

            if ((data.filter.status && (*data.filter.status != offer.m_status)) ||
                (data.filter.swapCoin && (*data.filter.swapCoin != offer.m_coin)))
            {
                continue;
            }

            const auto it = std::find_if(publicOffers.begin(), publicOffers.end(),
                [&offer](const SwapOffer& offerFromBoard) {
                    return offer.m_txId == offerFromBoard.m_txId;
                });

            if (it != publicOffers.end())
            {
                offer = MirrorSwapTxParams(offer);
                offer.m_publisherId = it->m_publisherId;
            }
            offers.push_back(offer);
        }

        doResponse(
            id,
            OffersList::Response
            {
                walletDB->getAddresses(true),
                walletDB->getCurrentHeight(),
                offers,
            });
    }

    void WalletApiHandler::onMessage(const JsonRpcId& id, const OffersBoard& data)
    {
        std::vector<SwapOffer> offers = _walletData.getAtomicSwapProvider().getSwapOffersBoard().getOffersList();
        auto walletDB = _walletData.getWalletDBPtr();
        if (!walletDB) {
            return doError(id, ApiError::NotOpenedError);
        }

        if (data.filter.swapCoin)
        {
            std::vector<SwapOffer> filteredOffers;
            filteredOffers.reserve(offers.size());

            std::copy_if(offers.begin(), offers.end(), std::back_inserter(filteredOffers),
                [&data](const auto& offer) { return offer.m_coin == *data.filter.swapCoin; });

            offers.swap(filteredOffers);
        }

        doResponse(
            id,
            OffersBoard::Response
            {
                walletDB->getAddresses(true),
                walletDB->getCurrentHeight(),
                offers,
            });
    }

    void WalletApiHandler::onMessage(const JsonRpcId& id, const CreateOffer& data)
    {
        try
        {
            checkSwapConnection(_walletData.getAtomicSwapProvider(), data.swapCoin);

            auto walletDB = _walletData.getWalletDBPtr();
            auto wallet = _walletData.getWalletPtr();
            if (!walletDB || !wallet) {
                return doError(id, ApiError::NotOpenedError);
            }

            // TODO need to unite with AcceptOffer
            Amount recommendedFeeRate = _walletData.getAtomicSwapProvider().getRecommendedFeeRate(data.swapCoin);

            if (recommendedFeeRate > 0 && data.swapFeeRate < recommendedFeeRate)
            {
                doError(id, ApiError::InvalidJsonRpc, kSwapFeeToLowRecommenededError);
                return;
            }

            Amount minFeeRate = _walletData.getAtomicSwapProvider().getMinFeeRate(data.swapCoin);

            if (minFeeRate > 0 && data.swapFeeRate < minFeeRate)
            {
                std::stringstream msg;
                msg << kSwapFeeToLowError << minFeeRate;
                doError(id, ApiError::InvalidJsonRpc, msg.str());
                return;
            }

            if (data.beamAmount <= data.beamFee)
            {
                doError(id, ApiError::InvalidJsonRpc, kBeamAmountToLowError);
                return;
            }

            if (!IsSwapAmountValid(data.swapCoin, data.swapAmount, data.swapFeeRate))
            {
                doError(id, ApiError::InvalidJsonRpc, kSwapAmountToLowError);
                return;
            }
            
            if (data.isBeamSide)
            {
                checkIsEnoughtBeamAmount(walletDB, data.beamAmount, data.beamFee);
            }
            else
            {
                bool isEnought = checkIsEnoughtSwapAmount(
                    _walletData.getAtomicSwapProvider(), data.swapCoin, data.swapAmount, data.swapFeeRate);
                if (!isEnought)
                {
                    doError(id, ApiError::InvalidJsonRpc, kSwapNotEnoughtSwapCoins);
                    return;
                }
            }

            auto txParameters = CreateSwapTransactionParameters();
            auto wid = createWID(walletDB.get(), data.comment);
            auto currentHeight = walletDB->getCurrentHeight();
            FillSwapTxParams(
                &txParameters,
                wid,
                currentHeight,
                data.beamAmount,
                data.beamFee,
                data.swapCoin,
                data.swapAmount,
                data.swapFeeRate,
                data.isBeamSide,
                data.offerLifetime);

            if (!data.comment.empty())
            {
                txParameters.SetParameter(TxParameterID::Message,
                    beam::ByteBuffer(data.comment.begin(), data.comment.end()));
            }

            auto txId = wallet->StartTransaction(txParameters);
            LOG_DEBUG() << "transaction created: " << txId;

            const auto& mirroredTxParams = MirrorSwapTxParams(txParameters);
            const auto& readyForTokenizeTxParams = PrepareSwapTxParamsForTokenization(mirroredTxParams);
            auto token = std::to_string(readyForTokenizeTxParams);

            doResponse(
                id,
                CreateOffer::Response
                {
                    walletDB->getAddresses(true),
                    currentHeight,
                    token,
                    txId
                });
        }
        catch (const NotEnoughtBeams & e)
        {
            doError(id, ApiError::SwapNotEnoughtBeams, e.what());
        }
        catch (const FailToConnectSwap & e)
        {
            doError(id, ApiError::SwapFailToConnect, e.what());
        }
        catch (...)
        {
            doError(id, ApiError::InternalErrorJsonRpc, "Transaction could not be created. Please look at logs.");
        }
    }

    void WalletApiHandler::onMessage(const JsonRpcId& id, const PublishOffer& data)
    {
        try
        {
            auto txParams = ParseParameters(data.token);
            if (!txParams)
                throw FailToParseToken();

            auto txId = txParams->GetTxID();
            if (!txId)
                throw FailToParseToken();

            auto walletDB = _walletData.getWalletDBPtr();
            if (!walletDB) {
                return doError(id, ApiError::NotOpenedError);
            }

            auto tx = walletDB->getTx(*txId);

            if (!tx)
            {
                doError(id, ApiError::InternalErrorJsonRpc, "Transaction not found.");
                return;
            }

            const auto& mirroredTxParams = MirrorSwapTxParams(*tx);
            const auto& readyForTokenizeTxParams = PrepareSwapTxParamsForTokenization(mirroredTxParams);
            SwapOffer offer(readyForTokenizeTxParams);

            if (offer.m_status == SwapOfferStatus::Pending)
            {
                offer.m_publisherId = *offer.GetParameter<WalletID>(TxParameterID::PeerID);
                _walletData.getAtomicSwapProvider().getSwapOffersBoard().publishOffer(offer);

                doResponse(id, PublishOffer::Response
                    {
                        walletDB->getAddresses(true),
                        walletDB->getCurrentHeight(),
                        offer
                    });
            }
        }
        catch (const FailToParseToken & e)
        {
            doError(id, ApiError::SwapFailToParseToken, e.what());
        }
        // handled: InvalidOfferException, ForeignOfferException, 
        //        OfferAlreadyPublishedException, ExpiredOfferException
        catch (const std::runtime_error & e)
        {
            std::stringstream ss;
            ss << "Failed to publish offer:" << e.what();

            doError(id, ApiError::InvalidJsonRpc, ss.str());
        }
    }

    void WalletApiHandler::onMessage(const JsonRpcId& id, const AcceptOffer& data)
    {
        try
        {
            auto txParams = ParseParameters(data.token);
            if (!txParams)
                throw FailToParseToken();
#ifdef BEAM_LIB_VERSION
            auto libVersion = txParams->GetParameter(beam::wallet::TxParameterID::LibraryVersion);
            if (libVersion)
            {
                std::string libVersionStr;
                beam::wallet::fromByteBuffer(*libVersion, libVersionStr);
                std::string myLibVersionStr = BEAM_LIB_VERSION;

                std::regex libVersionRegex("\\d{1,}\\.\\d{1,}\\.\\d{4,}");
                if (std::regex_match(libVersionStr, libVersionRegex) &&
                    std::lexicographical_compare(
                        myLibVersionStr.begin(),
                        myLibVersionStr.end(),
                        libVersionStr.begin(),
                        libVersionStr.end(),
                        std::less<char>{}))
                {
                    LOG_WARNING() <<
                        "This token generated by newer Beam library version(" << libVersionStr << ")\n" <<
                        "Your version is: " << myLibVersionStr << " Please, check for updates.";
                }
            }
#endif  // BEAM_LIB_VERSION

            auto txId = txParams->GetTxID();
            if (!txId)
                throw FailToParseToken();

            auto publicOffer = getOfferFromBoardByTxId(
                _walletData.getAtomicSwapProvider().getSwapOffersBoard().getOffersList(), *txId);

            auto walletDB = _walletData.getWalletDBPtr();
            auto wallet = _walletData.getWalletPtr();
            if (!walletDB) {
                return doError(id, ApiError::NotOpenedError);
            }

            auto myAddresses = walletDB->getAddresses(true);

            if (publicOffer)
            {
                // compare public offer and token
                if (!checkPublicOffer(*txParams, *publicOffer))
                {
                    doError(id, ApiError::InvalidJsonRpc, "Wrong offer params.");
                }

                if (storage::isMyAddress(myAddresses, publicOffer->m_publisherId))
                    throw FailToAcceptOwnOffer();
            }
            else
            {
                auto peerId = txParams->GetParameter<WalletID>(TxParameterID::PeerID);

                if (!peerId)
                    throw FailToParseToken();

                if (storage::isMyAddress(myAddresses, *peerId))
                    throw FailToAcceptOwnOffer();
            }

            if (auto tx = walletDB->getTx(*txId); tx)
            {
                doError(id, ApiError::InvalidJsonRpc, "Offer already accepted.");
                return;
            }

            auto beamAmount = txParams->GetParameter<Amount>(TxParameterID::Amount);
            auto swapAmount = txParams->GetParameter<Amount>(TxParameterID::AtomicSwapAmount);
            auto swapCoin = txParams->GetParameter<AtomicSwapCoin>(TxParameterID::AtomicSwapCoin);
            auto isBeamSide = txParams->GetParameter<bool>(TxParameterID::AtomicSwapIsBeamSide);
            if (!beamAmount || !swapAmount || !swapCoin || !isBeamSide)
            {
                throw FailToParseToken();
            }

            Amount recommendedFeeRate = _walletData.getAtomicSwapProvider().getRecommendedFeeRate(*swapCoin);

            if (recommendedFeeRate > 0 && data.swapFeeRate < recommendedFeeRate)
            {
                doError(id, ApiError::InvalidJsonRpc, kSwapFeeToLowRecommenededError);
                return;
            }

            Amount minFeeRate = _walletData.getAtomicSwapProvider().getMinFeeRate(*swapCoin);

            if (minFeeRate > 0 && data.swapFeeRate < minFeeRate)
            {
                std::stringstream msg;
                msg << kSwapFeeToLowError << minFeeRate;
                doError(id, ApiError::InvalidJsonRpc, msg.str());
                return;
            }

            if (*beamAmount <= data.beamFee)
            {
                doError(id, ApiError::InvalidJsonRpc, kBeamAmountToLowError);
                return;
            }

            if (!IsSwapAmountValid(*swapCoin, *swapAmount, data.swapFeeRate))
            {
                doError(id, ApiError::InvalidJsonRpc, kSwapAmountToLowError);
                return;
            }

            checkSwapConnection(_walletData.getAtomicSwapProvider(), *swapCoin);

            if (*isBeamSide)
            {
                checkIsEnoughtBeamAmount(walletDB, *beamAmount, data.beamFee);
            }
            else
            {
                bool isEnought = checkIsEnoughtSwapAmount(
                    _walletData.getAtomicSwapProvider(), *swapCoin, *swapAmount, data.swapFeeRate);
                if (!isEnought)
                {
                    doError(id, InvalidJsonRpc, kSwapNotEnoughtSwapCoins);
                    return;
                }
            }

            auto wid = createWID(walletDB.get(), data.comment);
            SwapOffer offer = SwapOffer(*txParams);
            offer.SetParameter(TxParameterID::MyID, wid);
            if (!data.comment.empty())
            {
                offer.SetParameter(TxParameterID::Message,
                    beam::ByteBuffer(data.comment.begin(),
                        data.comment.end()));
            }

            FillSwapFee(&offer, data.beamFee, data.swapFeeRate, *isBeamSide);

            wallet->StartTransaction(offer);
            offer.m_status = SwapOfferStatus::InProgress;
            if (!publicOffer)
                offer.DeleteParameter(TxParameterID::MyID);

            doResponse(
                id,
                AcceptOffer::Response
                {
                    myAddresses,
                    walletDB->getCurrentHeight(),
                    offer
                });
        }
        catch (const FailToParseToken & e)
        {
            doError(id, ApiError::SwapFailToParseToken, e.what());
        }
        catch (const FailToAcceptOwnOffer & e)
        {
            doError(id, ApiError::SwapFailToAcceptOwnOffer, e.what());
        }
        catch (const NotEnoughtBeams & e)
        {
            doError(id, ApiError::SwapNotEnoughtBeams, e.what());
        }
        catch (const FailToConnectSwap & e)
        {
            doError(id, ApiError::SwapFailToConnect, e.what());
        }
        catch (const std::runtime_error & e)
        {
            doError(id, ApiError::InvalidJsonRpc, e.what());
        }
    }

    void WalletApiHandler::onMessage(const JsonRpcId& id, const OfferStatus& data)
    {
        auto walletDB = _walletData.getWalletDBPtr();
        if (!walletDB) {
            return doError(id, ApiError::NotOpenedError);
        }

        auto publicOffer = getOfferFromBoardByTxId(
            _walletData.getAtomicSwapProvider().getSwapOffersBoard().getOffersList(), data.txId);
        SwapOffer offer;
        if (auto tx = walletDB->getTx(data.txId); tx)
        {
            offer = SwapOffer(*tx);
        }
        else if (publicOffer)
        {
            offer = *publicOffer;
        }
        else
        {
            doError(id, ApiError::InvalidJsonRpc, "It is not my offer.");
            return;
        }

        doResponse(id, OfferStatus::Response{ walletDB->getCurrentHeight(), offer });
    }

    void WalletApiHandler::onMessage(const JsonRpcId& id, const DecodeToken& data)
    {
        try
        {
            auto txParams = ParseParameters(data.token);
            if (!txParams)
                throw FailToParseToken();

            auto txId = txParams->GetTxID();
            if (!txId)
                throw FailToParseToken();

            auto walletDB = _walletData.getWalletDBPtr();
            if (!walletDB) {
                return doError(id, ApiError::NotOpenedError);
            }

            auto peerId = txParams->GetParameter<WalletID>(TxParameterID::PeerID);
            if (!peerId)
                throw FailToParseToken();

            SwapOffer offer;
            bool isMyOffer = false;
            auto myAddresses = walletDB->getAddresses(true);
            if (!storage::isMyAddress(myAddresses, *peerId))
            {
                offer = SwapOffer(*txParams);
            }
            else
            {
                isMyOffer = true;
                // TODO roman.strilets: maybe it is superfluous
                auto mirroredTxParams = MirrorSwapTxParams(*txParams, false);
                offer = SwapOffer(mirroredTxParams);
            }

            auto publicOffer = getOfferFromBoardByTxId(
                _walletData.getAtomicSwapProvider().getSwapOffersBoard().getOffersList(), *txId);

            bool isPublic = !!publicOffer;

            doResponse(
                id,
                DecodeToken::Response
                {
                    offer,
                    isMyOffer,
                    isPublic
                });
        }
        catch (const FailToParseToken & e)
        {
            doError(id, ApiError::SwapFailToParseToken, e.what());
        }
    }

    void WalletApiHandler::onMessage(const JsonRpcId& id, const GetBalance& data)
    {
        try
        {
            checkSwapConnection(_walletData.getAtomicSwapProvider(), data.coin);

            Amount available = _walletData.getAtomicSwapProvider().getCoinAvailable(data.coin);

            doResponse(id, GetBalance::Response{ available });
        }
        catch (const FailToConnectSwap & e)
        {
            doError(id, ApiError::SwapFailToConnect, e.what());
        }
    }

    void WalletApiHandler::onMessage(const JsonRpcId& id, const RecommendedFeeRate& data)
    {
        try
        {
            checkSwapConnection(_walletData.getAtomicSwapProvider(), data.coin);

            Amount feeRate = _walletData.getAtomicSwapProvider().getRecommendedFeeRate(data.coin);

            doResponse(id, RecommendedFeeRate::Response{ feeRate });
        }
        catch (const FailToConnectSwap& e)
        {
            doError(id, ApiError::SwapFailToConnect, e.what());
        }
    }
#endif  // BEAM_ATOMIC_SWAP_SUPPORT

} // namespace beam::wallet
