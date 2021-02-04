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
#include "wallet_api.h"
#include "wallet/core/common_utils.h"
#include "bvm/ManagerStd.h"

#ifdef BEAM_ATOMIC_SWAP_SUPPORT
#include "wallet/client/extensions/offers_board/swap_offer_token.h"
#include "wallet/transactions/swaps/common.h"
#include "wallet/transactions/swaps/swap_transaction.h"
#include "wallet/transactions/swaps/swap_tx_description.h"
#endif  // BEAM_ATOMIC_SWAP_SUPPORT

namespace beam::wallet {
    namespace {
        // This is for jscript compatibility
        // Number.MAX_SAFE_INTEGER
        const auto MAX_ALLOWED_INT = AmountBig::Type(9'007'199'254'740'991U);
    }

    CoinIDList readCoinsParameter(const JsonRpcId& id, const json& params)
    {
    CoinIDList coins;

    if (!params["coins"].is_array() || params["coins"].size() <= 0)
    {
        throw jsonrpc_exception(ApiError::InvalidJsonRpc, "Coins parameter must be an array of strings (coin IDs).");
    }

    for (const auto& cid : params["coins"])
    {
        if (!cid.is_string())
        {
            throw jsonrpc_exception(ApiError::InvalidJsonRpc, "Coin ID in the coins array must be a string.");
        }

        std::string sCid = cid;
        auto coinId = Coin::FromString(sCid);
        if (!coinId)
        {
            const auto errmsg = std::string("Invalid 'coin ID' parameter: ") + std::string(sCid);
            throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, errmsg);
        }

        coins.push_back(*coinId);
    }

    return coins;
    }

    uint64_t readSessionParameter(const JsonRpcId& id, const json& params)
    {
    if (params["session"].is_number_unsigned() && params["session"] > 0)
    {
        return params["session"].get<uint64_t>();
    }

    throw jsonrpc_exception(ApiError::InvalidJsonRpc, "Invalid 'session' parameter.");
    }

    bool readAssetsParameter(const JsonRpcId& id, const json& params)
    {
    if (auto oassets = WalletApi::getOptionalParam<bool>(params, "assets"))
    {
        return *oassets;
    }
    return false;
    }

    boost::optional<Asset::ID> readAssetIdParameter(const JsonRpcId& id, const json& params)
    {
        return WalletApi::getOptionalParam<uint32_t>(params, "asset_id");
    }

    WalletApi::WalletApi(
            IWalletDB::Ptr wdb,
            Wallet::Ptr wallet,
            ISwapsProvider::Ptr swaps,
            IShadersManager::Ptr contracts,
            ACL acl
        )
        : ApiBase(std::move(acl))
        , _wdb(std::move(wdb))
        , _wallet(std::move(wallet))
        , _swaps(std::move(swaps))
        , _contracts(std::move(contracts))
    {
        #define REG_FUNC(api, name, writeAccess) \
        _methods[name] = {BIND_THIS_MEMFN(on##api##Message), writeAccess};
        WALLET_API_METHODS(REG_FUNC)
        #undef REG_FUNC

        #define REG_ALIASES_FUNC(aliasName, api, name, writeAccess) \
        _methods[aliasName] = {BIND_THIS_MEMFN(on##api##Message), writeAccess};
        WALLET_API_METHODS_ALIASES(REG_ALIASES_FUNC)
        #undef REG_ALIASES_FUNC
        //WALLET_API_METHODS_ALIASES
    }

    static void FillAddressData(const JsonRpcId& id, const json& params, AddressData& data)
    {
        if (auto comment = WalletApi::getOptionalParam<std::string>(params, "comment"))
        {
            data.comment = *comment;
        }

        if (auto expiration = WalletApi::getOptionalParam<NonEmptyString>(params, "expiration"))
        {
            static std::map<std::string, AddressData::Expiration> Items =
            {
                {"expired", AddressData::Expired},
                {"24h",  AddressData::OneDay},
                {"never", AddressData::Never},
            };

            if(Items.count(*expiration) == 0)
            {
                throw jsonrpc_exception(ApiError::InvalidJsonRpc, "Unknown value for the 'expiration' parameter.");
            }

            data.expiration = Items[*expiration];
        }
    }

    void WalletApi::onCreateAddressMessage(const JsonRpcId& id, const json& params)
    {
        CreateAddress createAddress;
        beam::wallet::FillAddressData(id, params, createAddress);
        onMessage(id, createAddress);
    }

    void WalletApi::onDeleteAddressMessage(const JsonRpcId& id, const json& params)
    {
        const auto address = getMandatoryParam<NonEmptyString>(params, "address");

        DeleteAddress deleteAddress;
        deleteAddress.address.FromHex(address);

        onMessage(id, deleteAddress);
    }

    void WalletApi::onEditAddressMessage(const JsonRpcId& id, const json& params)
    {
        const auto address = getMandatoryParam<NonEmptyString>(params, "address");

        if (!hasParam(params, "comment") && !hasParam(params, "expiration"))
        {
            throw jsonrpc_exception(ApiError::InvalidJsonRpc, "Comment or Expiration parameter must be specified.");
        }

        EditAddress editAddress;
        editAddress.address.FromHex(address);

        beam::wallet::FillAddressData(id, params, editAddress);
        onMessage(id, editAddress);
    }

    void WalletApi::onAddrListMessage(const JsonRpcId& id, const json& params)
    {
        const auto own = getMandatoryParam<bool>(params, "own");

        AddrList addrList = {};
        addrList.own = own;

        onMessage(id, addrList);
    }

    void WalletApi::onValidateAddressMessage(const JsonRpcId& id, const json& params)
    {
        const auto address = getMandatoryParam<NonEmptyString>(params, "address");

        ValidateAddress validateAddress;
        validateAddress.address = address;

        onMessage(id, validateAddress);
    }

    void WalletApi::onSendMessage(const JsonRpcId& id, const json& params)
    {
        Send send;

        const std::string addressOrToken = getMandatoryParam<NonEmptyString>(params, "address");
        if(auto txParams = ParseParameters(addressOrToken))
        {
            send.txParameters = *txParams;
        }
        else
        {
            throw jsonrpc_exception(ApiError::InvalidAddress , "Invalid receiver address or token.");
        }

        send.value = getMandatoryParam<PositiveAmount>(params, "value");
        send.assetId = readAssetIdParameter(id, params);

        if (send.assetId && *send.assetId != Asset::s_InvalidID)
        {
            checkCAEnabled(id);
        }

        if (hasParam(params, "coins"))
        {
            // TODO: may be no check and optional read
            send.coins = readCoinsParameter(id, params);
        }
        else if (hasParam(params, "session"))
        {
            // TODO: may be no check and optional read
            send.session = readSessionParameter(id, params);
        }

        if (auto peerID = send.txParameters.GetParameter<WalletID>(TxParameterID::PeerID); peerID)
        {
            send.address = *peerID;
            if (std::to_string(*peerID) != addressOrToken)
            {
                send.txParameters.SetParameter(beam::wallet::TxParameterID::OriginalToken, addressOrToken);
            }
        }
        else
        {
            throw jsonrpc_exception(ApiError::InvalidAddress , "Invalid receiver address.");
        }

        if (auto fromParam = getOptionalParam<NonEmptyString>(params, "from"))
        {
            WalletID from(Zero);
            if (from.FromHex(*fromParam))
            {
                send.from = from;
            }
            else
            {
                throw jsonrpc_exception(ApiError::InvalidAddress, "Invalid sender address.");
            }
        }

        send.fee = getBeamFeeParam(params);

        if (auto comment = getOptionalParam<std::string>(params, "comment"))
        {
            send.comment = *comment;
        }

        send.txId = getOptionalParam<ValidTxID>(params, "txId");
        onMessage(id, send);
    }

    void WalletApi::onStatusMessage(const JsonRpcId& id, const json& params)
    {
        Status status = {};
        status.txId = getMandatoryParam<ValidTxID>(params, "txId");
        onMessage(id, status);
    }

    void WalletApi::onSplitMessage(const JsonRpcId& id, const json& params)
    {
        Split split;
        split.assetId = readAssetIdParameter(id, params);
        if (split.assetId && *split.assetId != Asset::s_InvalidID)
        {
            checkCAEnabled(id);
        }

        const json& coins = getMandatoryParam<NonEmptyJsonArray>(params, "coins");
        for (const auto& amount: coins)
        {
            if(!amount.is_number_unsigned() || amount == 0)
            {
                throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc,
                                        "Coin amount must be non zero 64bit unsigned integer.");
            }

            split.coins.push_back(amount);
        }

        auto minimumFee = std::max(wallet::GetMinimumFee(split.coins.size() + 1), kMinFeeInGroth); // +1 extra output for change
        split.fee = getBeamFeeParam(params, "fee", minimumFee);
        split.txId = getOptionalParam<ValidTxID>(params, "txId");

        onMessage(id, split);
    }

    void WalletApi::onTxCancelMessage(const JsonRpcId& id, const json& params)
    {
        TxCancel txCancel = {};
        txCancel.txId = getMandatoryParam<ValidTxID>(params, "txId");
        onMessage(id, txCancel);
    }

    void WalletApi::onTxDeleteMessage(const JsonRpcId& id, const json& params)
    {
        TxDelete txDelete = {};
        txDelete.txId = getMandatoryParam<ValidTxID>(params, "txId");
        onMessage(id, txDelete);
    }

    template<typename T>
    void ReadAssetParams(const JsonRpcId& id, const json& params, T& data)
    {
        if (ApiBase::hasParam(params, "asset_meta"))
        {
            if (!params["asset_meta"].is_string() || params["asset_meta"].get<std::string>().empty())
            {
                throw jsonrpc_exception(ApiError::InvalidJsonRpc, "meta should be non-empty string");
            }
            data.assetMeta = params["asset_meta"].get<std::string>();
        }
        else if(ApiBase::hasParam(params, "asset_id"))
        {
            data.assetId = readAssetIdParameter(id, params);
        }
        else
        {
            throw jsonrpc_exception(ApiError::InvalidJsonRpc, "asset_id or meta is required");
        }
    }

    void WalletApi::onIssueMessage(const JsonRpcId& id, const json& params)
    {
        onIssueConsumeMessage<Issue>(true, id, params);
    }

    void WalletApi::onConsumeMessage(const JsonRpcId& id, const json& params)
    {
        onIssueConsumeMessage<Consume>(true, id, params);
    }

    void WalletApi::checkCAEnabled(const JsonRpcId& id)
    {
        TxFailureReason res = wallet::CheckAssetsEnabled(MaxHeight);
        if (TxFailureReason::Count != res)
        {
            throw jsonrpc_exception(ApiError::NotSupported, GetFailureMessage(res));
        }
    }

    template<typename T>
    void WalletApi::onIssueConsumeMessage(bool issue, const JsonRpcId& id, const json& params)
    {
        checkCAEnabled(id);

        T data;
        data.value = getMandatoryParam<PositiveUnit64>(params, "value");

        if (hasParam(params, "coins"))
        {
            data.coins = readCoinsParameter(id, params);
        }
        else if (hasParam(params, "session"))
        {
            data.session = readSessionParameter(id, params);
        }

        ReadAssetParams(id, params, data);
        data.fee = getBeamFeeParam(params);
        data.txId = getOptionalParam<ValidTxID>(params, "txId");

        onMessage(id, data);
    }

    template void WalletApi::onIssueConsumeMessage<Issue>(bool issue, const JsonRpcId& id, const json& params);
    template void WalletApi::onIssueConsumeMessage<Consume>(bool issue, const JsonRpcId& id, const json& params);

    void WalletApi::onGetAssetInfoMessage(const JsonRpcId& id, const json& params)
    {
        GetAssetInfo data;
        ReadAssetParams(id, params, data);

        onMessage(id, data);
    }

    void WalletApi::onSetConfirmationsCountMessage(const JsonRpcId& id, const json& params)
    {
        SetConfirmationsCount data;
        data.count = getMandatoryParam<uint32_t>(params, "count");
        onMessage(id, data);
    }

    void WalletApi::onGetConfirmationsCountMessage(const JsonRpcId& id, const json& params)
    {
        GetConfirmationsCount data;
        onMessage(id, data);
    }

    void WalletApi::onTxAssetInfoMessage(const JsonRpcId& id, const json& params)
    {
        checkCAEnabled(id);

        TxAssetInfo data;
        ReadAssetParams(id, params, data);
        data.txId = getOptionalParam<ValidTxID>(params, "txId");

        onMessage(id, data);
    }

    void WalletApi::onGetUtxoMessage(const JsonRpcId& id, const json& params)
    {
        GetUtxo getUtxo;
        getUtxo.withAssets = readAssetsParameter(id, params);

        if (auto count = getOptionalParam<PositiveUint32>(params, "count"))
        {
            getUtxo.count = *count;
        }

        if (hasParam(params, "filter"))
        {
            getUtxo.filter.assetId = readAssetIdParameter(id, params["filter"]);
        }

        if (auto skip = getOptionalParam<uint32_t>(params, "skip"))
        {
            getUtxo.skip = *skip;
        }

        if (hasParam(params, "sort"))
        {
            if (hasParam(params["sort"], "field") && params["sort"]["field"].is_string())
            {
                getUtxo.sort.field = params["sort"]["field"].get<std::string>();
            }

            if (hasParam(params["sort"], "direction") && params["sort"]["direction"].is_string())
            {
                auto direction = params["sort"]["direction"].get<std::string>();
                if (direction != "desc" && direction != "asc")
                {
                    throw jsonrpc_exception(ApiError::InvalidJsonRpc, "Invalid 'direction' parameter. Use 'desc' or 'asc'.");
                }
                getUtxo.sort.desc = direction == "desc";
            }
        }

        onMessage(id, getUtxo);
    }

    void WalletApi::onLockMessage(const JsonRpcId& id, const json& params)
    {
        Lock lock;
        lock.session = readSessionParameter(id, params);
        lock.coins = readCoinsParameter(id, params);

        onMessage(id, lock);
    }

    void WalletApi::onUnlockMessage(const JsonRpcId& id, const json& params)
    {
        Unlock unlock;
        unlock.session = readSessionParameter(id, params);

        onMessage(id, unlock);
    }

    void WalletApi::onTxListMessage(const JsonRpcId& id, const json& params)
    {
        TxList txList;
        txList.withAssets = readAssetsParameter(id, params);

        if (hasParam(params, "filter"))
        {
            if (hasParam(params["filter"], "status")
                && params["filter"]["status"].is_number_unsigned())
            {
                txList.filter.status = (TxStatus)params["filter"]["status"];
            }

            if (hasParam(params["filter"], "height")
                && params["filter"]["height"].is_number_unsigned())
            {
                txList.filter.height = (Height)params["filter"]["height"];
            }

            txList.filter.assetId = readAssetIdParameter(id, params["filter"]);
        }

        if (auto count = getOptionalParam<PositiveUint32>(params, "count"))
        {
            txList.count = *count;
        }

        if (auto skip = getOptionalParam<PositiveUint32>(params, "skip"))
        {
            txList.skip = *skip;
        }

        onMessage(id, txList);
    }

    void WalletApi::onWalletStatusApiMessage(const JsonRpcId& id, const json& params)
    {
        WalletStatusApi walletStatus;
        walletStatus.withAssets = readAssetsParameter(id, params);
        onMessage(id, walletStatus);
    }

    void WalletApi::onGenerateTxIdMessage(const JsonRpcId& id, const json& params)
    {
        GenerateTxId generateTxId;
        onMessage(id, generateTxId);
    }

    void WalletApi::onExportPaymentProofMessage(const JsonRpcId& id, const json& params)
    {
        ExportPaymentProof data = {};
        data.txId = getMandatoryParam<ValidTxID>(params, "txId");
        onMessage(id, data);
    }

    void WalletApi::onVerifyPaymentProofMessage(const JsonRpcId& id, const json& params)
    {
        VerifyPaymentProof data;

        const auto proof = getMandatoryParam<NonEmptyString>(params, "payment_proof");
        data.paymentProof = from_hex(proof);

        onMessage(id, data);
    }

#ifdef BEAM_ATOMIC_SWAP_SUPPORT

    void WalletApi::onOffersListMessage(const JsonRpcId& id, const json& params)
    {
        OffersList offersList;

        if (hasParam(params, "filter"))
        {
            if (hasParam(params["filter"], "swapCoin"))
            {
                if (params["filter"]["swapCoin"].is_string())
                {
                    offersList.filter.swapCoin = from_string(params["filter"]["swapCoin"]);
                }
            }

            if (hasParam(params["filter"], "status"))
            {
                if (params["filter"]["status"].is_number_unsigned())
                {
                    offersList.filter.status = static_cast<SwapOfferStatus>(params["filter"]["status"]);
                }
            }
        }

        onMessage(id, offersList);
    }

    void WalletApi::onOffersBoardMessage(const JsonRpcId& id, const json& params)
    {
        OffersBoard offersBoard;

        if (hasParam(params, "filter"))
        {
            if (hasParam(params["filter"], "swapCoin"))
            {
                if (params["filter"]["swapCoin"].is_string())
                {
                    offersBoard.filter.swapCoin = from_string(params["filter"]["swapCoin"]);
                }
            }
        }

        onMessage(id, offersBoard);
    }

    #ifdef BEAM_ATOMIC_SWAP_SUPPORT
void AddSwapTxDetailsToJson(const TxDescription& tx, json& msg)
{
    SwapTxDescription swapTx(tx);

    msg["is_beam_side"] = swapTx.isBeamSide();
    msg["swap_value"] = swapTx.getSwapAmount();

    auto fee = swapTx.getFee();
    if (fee)
    {
        msg["fee"] = *fee;
    }
    auto feeRate = swapTx.getSwapCoinFeeRate();
    if (feeRate)
    {
        msg["swap_fee_rate"] = *feeRate;
    }

    auto beamLockTxKernelID = swapTx.getBeamTxKernelId<SubTxIndex::BEAM_LOCK_TX>();
    if (beamLockTxKernelID)
    {
        msg["beam_lock_kernel_id"] = *beamLockTxKernelID;
    }

    std::string coinName = std::to_string(swapTx.getSwapCoin());
    std::locale loc;
    std::transform(coinName.begin(),
                   coinName.end(),
                   coinName.begin(),
                   [&loc](char c) -> char { return std::tolower(c, loc); });
    if (!coinName.empty())
    {
        msg["swap_coin"] = coinName;
        coinName.push_back('_');
    }

    auto swapCoinLockTxID = swapTx.getSwapCoinTxId<SubTxIndex::LOCK_TX>();
    if (swapCoinLockTxID)
    {
        std::string lockTxIdStr = "lock_tx_id";
        msg[coinName + lockTxIdStr] = *swapCoinLockTxID;
    }
    auto swapCoinLockTxConfirmations = swapTx.getSwapCoinTxConfirmations<SubTxIndex::LOCK_TX>();
    if (swapCoinLockTxConfirmations && swapTx.isBeamSide())
    {
        std::string lockTxConfirmationsStr = "lock_tx_confirmations";
        msg[coinName + lockTxConfirmationsStr] = *swapCoinLockTxConfirmations;
    }

    auto swapCoinRedeemTxID = swapTx.getSwapCoinTxId<SubTxIndex::REDEEM_TX>();
    if (swapCoinRedeemTxID && swapTx.isBeamSide() && swapTx.isLockTxProofReceived())
    {
        std::string redeemTxIdStr = "redeem_tx_id";
        msg[coinName + redeemTxIdStr] = *swapCoinRedeemTxID;
    }
    auto swapCoinRedeemTxConfirmations = swapTx.getSwapCoinTxConfirmations<SubTxIndex::REDEEM_TX>();
    if (swapCoinRedeemTxConfirmations && swapTx.isBeamSide() && swapTx.isLockTxProofReceived())
    {
        std::string redeemTxConfirmationsStr = "redeem_tx_confirmations";
        msg[coinName + redeemTxConfirmationsStr] = *swapCoinRedeemTxConfirmations;
    }

    auto failureReason = swapTx.getFailureReason();
    if (failureReason)
    {
        msg["failure_reason"] = GetFailureMessage(*failureReason);
    }
}
#endif // BEAM_ATOMIC_SWAP_SUPPORT

void GetStatusResponseJson(const TxDescription& tx,
    json& msg,
    Height txHeight,
    Height systemHeight,
    bool showIdentities = false)
{
    std::unique_ptr<TxStatusInterpreter> statusInterpreter = nullptr;
    if (tx.m_txType == TxType::Simple)
    {
        struct ApiTxStatusInterpreter : public TxStatusInterpreter
        {
            ApiTxStatusInterpreter(const TxParameters& txParams) : TxStatusInterpreter(txParams) {};
            virtual ~ApiTxStatusInterpreter() {}
            std::string getStatus() const override
            {
                if (m_status == TxStatus::Registering)
                    return m_selfTx ? "self sending" : (m_sender ? "sending" : "receiving");
                return TxStatusInterpreter::getStatus();
            }
        };
        statusInterpreter = std::make_unique<ApiTxStatusInterpreter>(tx);
    }
    else if (tx.m_txType >= TxType::AssetIssue && tx.m_txType <= TxType::AssetInfo)
    {
        struct ApiAssetTxStatusInterpreter : public AssetTxStatusInterpreter
        {
            ApiAssetTxStatusInterpreter(const TxParameters& txParams) : AssetTxStatusInterpreter(txParams) {};
            std::string getStatus() const override
            {
                if (m_status == TxStatus::Registering)
                    return m_selfTx ? "self sending" : (m_sender ? "sending" : "receiving");
                return AssetTxStatusInterpreter::getStatus();
            }
        };
        statusInterpreter = std::make_unique<AssetTxStatusInterpreter>(tx);
    }
    else if (tx.m_txType == TxType::AtomicSwap)
    {
        #ifdef BEAM_ATOMIC_SWAP_SUPPORT
        statusInterpreter = std::make_unique<SwapTxStatusInterpreter>(tx);
        #endif
    }
    else if (tx.m_txType == TxType::Contract)
    {
        statusInterpreter = std::make_unique<ContractTxStatusInterpreter>(tx);
    }
    msg = json
    {
        {"txId", TxIDToString(tx.m_txId)},
        {"status", tx.m_status},
        {"status_string", statusInterpreter ? statusInterpreter->getStatus() : "unknown"},
        {"sender", std::to_string(tx.m_sender ? tx.m_myId : tx.m_peerId)},
        {"receiver", std::to_string(tx.m_sender ? tx.m_peerId : tx.m_myId)},
        {"value", tx.m_amount},
        {"comment", std::string{ tx.m_message.begin(), tx.m_message.end() }},
        {"create_time", tx.m_createTime},
        {"asset_id", tx.m_assetId},
        {"tx_type", tx.m_txType},
        {"tx_type_string", tx.getTxTypeString()}
    };

    if (tx.m_txType != TxType::AtomicSwap)
    {
        msg["fee"] = tx.m_fee;
        msg["income"] = !tx.m_sender;
    }

    if (tx.m_txType == TxType::AssetIssue || tx.m_txType == TxType::AssetConsume || tx.m_txType == TxType::AssetInfo)
    {
        msg["asset_meta"] = tx.m_assetMeta;
    }

    if (tx.m_txType == TxType::Contract)
    {
        std::vector<beam::bvm2::ContractInvokeData> vData;
        if(tx.GetParameter(TxParameterID::ContractDataPacked, vData))
        {
            for (const auto& data: vData)
            {
                auto ivdata = json {
                   {"contract_id", data.m_Cid.str()}
                };
                msg["invoke_data"].push_back(ivdata);
            }
        }
    }

    if (txHeight > 0)
    {
        msg["height"] = txHeight;

        if (systemHeight >= txHeight)
        {
            msg["confirmations"] = systemHeight - txHeight;
        }
    }

    if (tx.m_status == TxStatus::Failed)
    {
        if (tx.m_txType != TxType::AtomicSwap)
        {
            msg["failure_reason"] = GetFailureMessage(tx.m_failureReason);
        }
    }
    else if (tx.m_status != TxStatus::Canceled)
    {
        if (tx.m_txType != TxType::AssetInfo && tx.m_txType != TxType::AtomicSwap)
        {
            msg["kernel"] = std::to_string(tx.m_kernelID);
        }
    }

    auto token = tx.GetParameter<std::string>(TxParameterID::OriginalToken);
    if (token)
    {
        msg["token"] = *token;
    }

    if (showIdentities)
    {
        auto senderIdentity = tx.getSenderIdentity();
        auto receiverIdentity = tx.getReceiverIdentity();
        if (!senderIdentity.empty() && !receiverIdentity.empty())
        {
            msg["sender_identity"] = senderIdentity;
            msg["receiver_identity"] = receiverIdentity;
        }
    }

#ifdef BEAM_ATOMIC_SWAP_SUPPORT
    if (tx.m_txType == TxType::AtomicSwap)
    {
        AddSwapTxDetailsToJson(tx, msg);
    }
#endif // BEAM_ATOMIC_SWAP_SUPPORT
}

#ifdef BEAM_ATOMIC_SWAP_SUPPORT
void throwIncorrectCurrencyError(const std::string& name, const JsonRpcId& id)
{
    throw jsonrpc_exception(ApiError::InvalidJsonRpc, "wrong currency message here.");
}

json OfferToJson(const SwapOffer& offer,
                 const std::vector<WalletAddress>& myAddresses,
                 const Height& systemHeight)
{
    const auto& publisherId = offer.m_publisherId;
    bool isOwnOffer = false;
    bool isPublic = false;
    if (publisherId.cmp(Zero))
    {
        isPublic = true;
        isOwnOffer = storage::isMyAddress(myAddresses, publisherId);
    }
    else
    {
        auto myId = offer.GetParameter<WalletID>(TxParameterID::MyID);
        // TODO roman.strilets should create new function for this code
        if (myId && storage::isMyAddress(myAddresses, *myId) && !offer.GetParameter<bool>(TxParameterID::IsInitiator).get())
            isOwnOffer = true;
    }

    bool isSendBeamOffer =
        (isOwnOffer && isPublic) ? !offer.isBeamSide() : offer.isBeamSide();

    Amount send =
        isSendBeamOffer ? offer.amountBeam() : offer.amountSwapCoin();
    Amount receive =
        isSendBeamOffer ? offer.amountSwapCoin() : offer.amountBeam();

    std::string sendCurrency =
        isSendBeamOffer ? "BEAM" : std::to_string(offer.swapCoinType());
    std::string receiveCurrency =
        isSendBeamOffer ? std::to_string(offer.swapCoinType()) : "BEAM";
    auto expiredHeight = offer.minHeight() + offer.peerResponseHeight();
    auto createTimeStr = format_timestamp(kTimeStampFormat3x3,offer.timeCreated() * 1000, false);

    json result {
        {"status", offer.m_status},
        {"status_string", swapOfferStatusToString(offer.m_status)},
        {"txId", TxIDToString(offer.m_txId)},
        {"send_amount", send},
        {"send_currency", sendCurrency},
        {"receive_amount", receive},
        {"receive_currency", receiveCurrency},
        {"time_created", createTimeStr},
        {"min_height", offer.minHeight()},
        {"height_expired", expiredHeight},
    };

    if (offer.m_status == SwapOfferStatus::Pending)
    {
        result["is_my_offer"] = isOwnOffer;
        result["is_public"] = isPublic;
        if (isOwnOffer && !isPublic)
        {
            const auto& mirroredTxParams = MirrorSwapTxParams(offer);
            const auto& readyForTokenizeTxParameters =
                PrepareSwapTxParamsForTokenization(mirroredTxParams);
            result["token"] = std::to_string(readyForTokenizeTxParameters);
        }
        else
        {
            const auto& txParameters = PrepareSwapTxParamsForTokenization(offer);
            result["token"] = std::to_string(txParameters);
        }
    }

    return result;
}

json OfferStatusToJson(const SwapOffer& offer, const Height& systemHeight)
{
    auto expiredHeight = offer.minHeight() + offer.peerResponseHeight();
    auto createTimeStr = format_timestamp(kTimeStampFormat3x3, offer.timeCreated() * 1000, false);
    json result{
        {"status", offer.m_status},
        {"status_string", swapOfferStatusToString(offer.m_status)},
        {"tx_id", TxIDToString(offer.m_txId)},
        {"time_created", createTimeStr},
        {"min_height", offer.minHeight()},
        {"height_expired", expiredHeight},
    };

    return result;
}

json TokenToJson(const SwapOffer& offer, bool isMyOffer = false, bool isPublic = false)
{
    // TODO roman.strilets: check isPublic in this code!!!
    bool isSendBeamOffer =
        (isMyOffer && isPublic) ? !offer.isBeamSide() : offer.isBeamSide();

    Amount send =
        isSendBeamOffer ? offer.amountBeam() : offer.amountSwapCoin();
    Amount receive =
        isSendBeamOffer ? offer.amountSwapCoin() : offer.amountBeam();

    std::string sendCurrency =
        isSendBeamOffer ? "BEAM" : std::to_string(offer.swapCoinType());
    std::string receiveCurrency =
        isSendBeamOffer ? std::to_string(offer.swapCoinType()) : "BEAM";

    auto createTimeStr = format_timestamp(kTimeStampFormat3x3,
        offer.timeCreated() * 1000,
        false);

    json result{
        {"tx_id", TxIDToString(offer.m_txId)},
        {"is_my_offer", isMyOffer},
        {"is_public", isPublic},
        {"send_amount", send},
        {"send_currency", sendCurrency},
        {"receive_amount", receive},
        {"receive_currency", receiveCurrency},
        {"min_height", offer.minHeight()},
        {"height_expired", offer.peerResponseHeight() + offer.minHeight()},
        {"time_created", createTimeStr},
    };

    return result;
}

Amount readSwapFeeRateParameter(const JsonRpcId& id, const json& params)
{
    return WalletApi::getMandatoryParam<PositiveAmount>(params, "fee_rate");
}

OfferInput collectOfferInput(const JsonRpcId& id, const json& params)
{
    OfferInput data;
    Amount sendAmount = WalletApi::getMandatoryParam<PositiveAmount>(params, "send_amount");

    const std::string send_currency = WalletApi::getMandatoryParam<NonEmptyString>(params, "send_currency");
    const AtomicSwapCoin sendCoin = from_string(send_currency);

    if (sendCoin == AtomicSwapCoin::Unknown && send_currency != "beam")
    {
        throwIncorrectCurrencyError("send_currency", id);
    }

    Amount receiveAmount = WalletApi::getMandatoryParam<PositiveAmount>(params, "receive_amount");

    const std::string receive_currency = WalletApi::getMandatoryParam<NonEmptyString>(params, "receive_currency");
    AtomicSwapCoin receiveCoin = from_string(receive_currency);

    if (receiveCoin == AtomicSwapCoin::Unknown && receive_currency != "beam")
    {
        throwIncorrectCurrencyError("receive_currency", id);
    }

    if (receive_currency != "beam" && send_currency != "beam")
    {
        throw jsonrpc_exception(ApiError::InvalidJsonRpc, R"('receive_currency' or 'send_currency' must be 'beam'.)");
    }

    if (sendCoin == receiveCoin)
    {
        throw jsonrpc_exception(ApiError::InvalidJsonRpc, R"('receive_currency' and 'send_currency' must be not same.)");
    }

    data.isBeamSide = sendCoin == AtomicSwapCoin::Unknown;
    data.swapCoin = data.isBeamSide ? receiveCoin : sendCoin;
    data.beamAmount = data.isBeamSide ? sendAmount : receiveAmount;
    data.swapAmount = data.isBeamSide ? receiveAmount : sendAmount;
    data.beamFee = WalletApi::getBeamFeeParam(params, "beam_fee");

    if (data.isBeamSide && data.beamAmount < data.beamFee)
    {
        throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, "beam swap amount is less than (default) fee.");
    }

    if (auto feeRate = readSwapFeeRateParameter(id, params); feeRate)
    {
        data.swapFeeRate = feeRate;
    }

    if (auto expires = WalletApi::getOptionalParam<PositiveHeight>(params, "offer_expires"))
    {
        data.offerLifetime = *expires;
    }

    if (auto comment = WalletApi::getOptionalParam<std::string>(params, "comment"))
    {
        data.comment = *comment;
    }

    return data;
}
#endif  // BEAM_ATOMIC_SWAP_SUPPORT

    void WalletApi::onCreateOfferMessage(const JsonRpcId& id, const json& params)
    {
        CreateOffer data = collectOfferInput(id, params);
        onMessage(id, data);
    }

    void WalletApi::onPublishOfferMessage(const JsonRpcId& id, const json& params)
    {
        const auto token = getMandatoryParam<NonEmptyString>(params, "token");
        if (!SwapOfferToken::isValid(token))
        {
            throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, "Parameter 'token' is not valid swap token.");
        }
        PublishOffer data{token};
        onMessage(id, data);
    }

    void WalletApi::onAcceptOfferMessage(const JsonRpcId& id, const json& params)
    {
        const auto token = getMandatoryParam<NonEmptyString>(params, "token");

        if (!SwapOfferToken::isValid(token))
        {
            throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, "Parameter 'token' is not valid swap token.");
        }

        AcceptOffer data;
        data.token = token;
        data.beamFee = getBeamFeeParam(params, "beam_fee");

        if (auto feeRate = readSwapFeeRateParameter(id, params); feeRate)
        {
            data.swapFeeRate = feeRate;
        }

        if (auto comment = WalletApi::getOptionalParam<std::string>(params, "comment"))
        {
            data.comment = *comment;
        }

        onMessage(id, data);
    }

    void WalletApi::onOfferStatusMessage(const JsonRpcId& id, const json& params)
    {
        OfferStatus offerStatus = {};
        offerStatus.txId = getMandatoryParam<ValidTxID>(params, "tx_id");
        onMessage(id, offerStatus);
    }

    void WalletApi::onDecodeTokenMessage(const JsonRpcId& id, const json& params)
    {
        const auto token = getMandatoryParam<NonEmptyString>(params, "token");

        if (!SwapOfferToken::isValid(token))
        {
            throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, "Parameter 'token' is not valid swap token.");
        }

        DecodeToken decodeToken{ token };
        onMessage(id, decodeToken);
    }

    void WalletApi::onGetBalanceMessage(const JsonRpcId& id, const json& params)
    {
        const auto coinName = getMandatoryParam<NonEmptyString>(params, "coin");
        const auto coin = from_string(coinName);

        if (coin == AtomicSwapCoin::Unknown)
        {
            throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, "Unknown coin.");
        }

        GetBalance data{ coin };
        onMessage(id, data);
    }

    void WalletApi::onRecommendedFeeRateMessage(const JsonRpcId& id, const json& params)
    {
        const auto coinName = getMandatoryParam<NonEmptyString>(params, "coin");
        AtomicSwapCoin coin = from_string(coinName);

        if (coin == AtomicSwapCoin::Unknown)
        {
            throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, "Unknown coin.");
        }

        RecommendedFeeRate data{ coin };
        onMessage(id, data);
    }

    void WalletApi::onInvokeContractMessage(const JsonRpcId &id, const json &params)
    {
        InvokeContract message;

        if(const auto contract = getOptionalParam<JsonArray>(params, "contract"))
        {
            const json& bytes = *contract;
            message.contract = bytes.get<std::vector<uint8_t>>();
        }

        if (const auto args = getOptionalParam<NonEmptyString>(params, "args"))
        {
            message.args = *args;
        }

        onMessage(id, message);
    }
#endif  // BEAM_ATOMIC_SWAP_SUPPORT

    void WalletApi::getResponse(const JsonRpcId& id, const CreateAddress::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", std::to_string(res.address)}
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const DeleteAddress::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", "done"}
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const InvokeContract::Response& res, json& msg)
    {
        msg = nlohmann::json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result",
                {
                    {"output", res.output},
                    {"txid",   TxIDToString(res.txid)}
                }
            }
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const EditAddress::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", "done"}
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const AddrList::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", json::array()}
        };

        for (auto& addr : res.list)
        {
            msg["result"].push_back(
            {
                {"address", std::to_string(addr.m_walletID)},
                {"comment", addr.m_label},
                {"category", addr.m_category},
                {"create_time", addr.getCreateTime()},
                {"duration", addr.m_duration},
                {"expired", addr.isExpired()},
                {"own", addr.isOwn()},
                {"own_id", addr.m_OwnID},
                {"own_id_str", std::to_string(addr.m_OwnID)},
            });
            if (addr.m_Identity != Zero)
            {
                msg["result"].back().push_back({ "identity", std::to_string(addr.m_Identity) });
            }
        }
    }

    void WalletApi::getResponse(const JsonRpcId& id, const ValidateAddress::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result",
                {
                    {"is_valid",  res.isValid},
                    {"is_mine",  res.isMine},
                }
            }
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const GetUtxo::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", json::array()}
        };

        for (auto& utxo : res.utxos)
        {
            std::string createTxId = utxo.m_createTxId.is_initialized() ? TxIDToString(*utxo.m_createTxId) : "";
            std::string spentTxId = utxo.m_spentTxId.is_initialized() ? TxIDToString(*utxo.m_spentTxId) : "";

            msg["result"].push_back(
            {
                {"id", utxo.toStringID()},
                {"asset_id", utxo.m_ID.m_AssetID},
                {"amount", utxo.m_ID.m_Value},
                {"type", (const char*)FourCC::Text(utxo.m_ID.m_Type)},
                {"maturity", utxo.get_Maturity(res.confirmations_count)},
                {"createTxId", createTxId},
                {"spentTxId", spentTxId},
                {"status", utxo.m_status},
                {"status_string", utxo.getStatusString()},
                {"session", utxo.m_sessionId}
            });
        }
    }

    void WalletApi::getResponse(const JsonRpcId& id, const Send::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result",
                {
                    {"txId", TxIDToString(res.txId)}
                }
            }
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const Issue::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result",
                {
                    {"txId", TxIDToString(res.txId)}
                }
            }
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const TxAssetInfo::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result",
                {
                    {"txId", TxIDToString(res.txId)}
                }
            }
        };
    }

    void WalletApi::getResponse(const JsonRpcId &id, const GetAssetInfo::Response &res, json &msg)
    {
        std::string strMeta;
        fromByteBuffer(res.AssetInfo.m_Metadata.m_Value, strMeta);

        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result",
                {
                    {"asset_id",      res.AssetInfo.m_ID},
                    {"lockHeight",    res.AssetInfo.m_LockHeight},
                    {"refreshHeight", res.AssetInfo.m_RefreshHeight},
                    {"ownerId",       res.AssetInfo.m_Owner.str()},
                    {"isOwned",       res.AssetInfo.m_IsOwned},
                    {"metadata",      strMeta}
                }
            }
        };

        auto& jsres = msg["result"];
        if(res.AssetInfo.m_Value <= MAX_ALLOWED_INT) {
            jsres["emission"] = AmountBig::get_Lo(res.AssetInfo.m_Value);
        }
        jsres["emission_str"] = std::to_string(res.AssetInfo.m_Value);
    }

    void WalletApi::getResponse(const JsonRpcId &id, const SetConfirmationsCount::Response &res, json &msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result",
                {
                    {"count", res.count}
                }
            }
        };
    }

    void WalletApi::getResponse(const JsonRpcId &id, const GetConfirmationsCount::Response &res, json &msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result",
                {
                    {"count", res.count}
                }
            }
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const Consume::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result",
                {
                    {"txId", TxIDToString(res.txId)}
                }
            }
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const Status::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", {}}
        };

        GetStatusResponseJson(res.tx, msg["result"], res.txHeight, res.systemHeight, true);
    }

    void WalletApi::getResponse(const JsonRpcId& id, const Split::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result",
                {
                    {"txId", TxIDToString(res.txId)}
                }
            }
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const TxCancel::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", res.result}
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const TxDelete::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", res.result}
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const TxList::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", json::array()}
        };

        for (const auto& resItem : res.resultList)
        {
            json item = {};
            GetStatusResponseJson(
                resItem.tx,
                item,
                resItem.txHeight,
                resItem.systemHeight);
            msg["result"].push_back(item);
        }
    }

    void WalletApi::getResponse(const JsonRpcId& id, const WalletStatusApi::Response& res, json& msg)
    {
        if (res.totals)
        {
            msg = json
            {
                {JsonRpcHeader, JsonRpcVersion},
                {"id", id},
                {"result",
                    {
                        {"current_height", res.currentHeight},
                        {"current_state_hash", to_hex(res.currentStateHash.m_pData, res.currentStateHash.nBytes)},
                        {"prev_state_hash", to_hex(res.prevStateHash.m_pData, res.prevStateHash.nBytes)},
                        {"difficulty", res.difficulty},
                        {"totals", json::array()}
                    }
                }
            };

            for(const auto& it: res.totals->allTotals)
            {
                const auto& totals = it.second;
                json jtotals;

                jtotals["asset_id"] = totals.AssetId;

                if (totals.Avail <= MAX_ALLOWED_INT) {
                    jtotals["available"] = AmountBig::get_Lo(totals.Avail);
                }
                jtotals["available_str"] = std::to_string(totals.Avail);

                if (totals.Incoming <= MAX_ALLOWED_INT) {
                    jtotals["receiving"] = AmountBig::get_Lo(totals.Incoming);
                }
                jtotals["receiving_str"] = std::to_string(totals.Incoming);

                if (totals.Outgoing <= MAX_ALLOWED_INT) {
                    jtotals["sending"] = AmountBig::get_Lo(totals.Outgoing);
                }
                jtotals["sending_str"] = std::to_string(totals.Outgoing);

                if (totals.Maturing <= MAX_ALLOWED_INT) {
                    jtotals["maturing"] = AmountBig::get_Lo(totals.Maturing);
                }
                jtotals["maturing_str"] = std::to_string(totals.Maturing);

                msg["result"]["totals"].push_back(jtotals);
            }
        }
        else
        {
            msg = json
            {
                {JsonRpcHeader, JsonRpcVersion},
                {"id", id},
                {"result",
                    {
                        {"current_height", res.currentHeight},
                        {"current_state_hash", to_hex(res.currentStateHash.m_pData, res.currentStateHash.nBytes)},
                        {"prev_state_hash", to_hex(res.prevStateHash.m_pData, res.prevStateHash.nBytes)},
                        {"available",  res.available},
                        {"receiving",  res.receiving},
                        {"sending",    res.sending},
                        {"maturing",   res.maturing},
                        {"difficulty", res.difficulty}
                    }
                }
            };
        }
    }

    void WalletApi::getResponse(const JsonRpcId& id, const GenerateTxId::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", TxIDToString(res.txId)}
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const Lock::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", res.result}
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const Unlock::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", res.result}
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const ExportPaymentProof::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result",
                {
                    {"payment_proof", to_hex(res.paymentProof.data(), res.paymentProof.size())}
                }
            }
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const VerifyPaymentProof::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result",
                {
                    {"is_valid", res.paymentInfo.IsValid()},

                    {"sender", std::to_string(res.paymentInfo.m_Sender)},
                    {"receiver", std::to_string(res.paymentInfo.m_Receiver)},
                    {"amount", res.paymentInfo.m_Amount},
                    {"kernel", std::to_string(res.paymentInfo.m_KernelID)},
                    {"asset_id", res.paymentInfo.m_AssetID}
                    //{"signature", std::to_string(res.paymentInfo.m_Signature)}
                }
            }
        };
    }

#ifdef BEAM_ATOMIC_SWAP_SUPPORT

    void WalletApi::getResponse(const JsonRpcId& id, const OffersList::Response& res, json& msg)
    {
        msg =
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", json::array()}
        };

        for (auto& offer : res.list)
        {
             msg["result"].push_back(
                OfferToJson(offer, res.addrList, res.systemHeight));
        }
    }

    void WalletApi::getResponse(const JsonRpcId& id, const OffersBoard::Response& res, json& msg)
    {
        msg =
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", json::array()}
        };

        for (auto& offer : res.list)
        {
            msg["result"].push_back(OfferToJson(offer, res.addrList, res.systemHeight));
        }
    }

    void WalletApi::getResponse(const JsonRpcId& id, const CreateOffer::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result",
            {
                {"txId", TxIDToString(res.txId)},
                {"token", res.token},
            }}
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const PublishOffer::Response& res, json& msg)
    {
        msg =
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", OfferToJson(res.offer, res.addrList, res.systemHeight)}
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const AcceptOffer::Response& res, json& msg)
    {
        msg =
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", OfferToJson(res.offer, res.addrList, res.systemHeight)}
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const OfferStatus::Response& res, json& msg)
    {
        msg =
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", OfferStatusToJson(res.offer, res.systemHeight)}
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const DecodeToken::Response& res, json& msg)
    {
        msg =
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", TokenToJson(res.offer, res.isMyOffer, res.isPublic)}
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const GetBalance::Response& res, json& msg)
    {
        msg =
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result",
            {
                {"available", res.available},
            }}
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const RecommendedFeeRate::Response& res, json& msg)
    {
        msg =
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result",
            {
                {"feerate", res.feeRate},
            }}
        };
    }
#endif  // BEAM_ATOMIC_SWAP_SUPPORT
}