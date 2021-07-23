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
#include <string_view>
#include <locale>
#include "v6_api.h"
#include "wallet/core/common_utils.h"
#include "utility/fsutils.h"
#include "bvm/ManagerStd.h"
#ifdef BEAM_ATOMIC_SWAP_SUPPORT
#include "wallet/transactions/swaps/swap_tx_description.h"
#endif // BEAM_ATOMIC_SWAP_SUPPORT

namespace beam::wallet
{
    CoinIDList readCoinsParameter(const JsonRpcId& id, const json& params)
    {
        CoinIDList coins;

        if (!params["coins"].is_array() || params["coins"].empty())
        {
            throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, "Coins parameter must be an array of strings (coin IDs).");
        }

        for (const auto& cid : params["coins"])
        {
            if (!cid.is_string())
            {
                throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, "Coin ID in the coins array must be a string.");
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

    boost::optional<Asset::ID> readOptionalAssetID(V6Api& api, const json& params)
    {
        auto aid = V6Api::getOptionalParam<uint32_t>(params, "asset_id");
        if (aid && *aid != Asset::s_InvalidID)
        {
            api.checkCAEnabled();
        }
        return aid;
    }

    Asset::ID readMandatoryNonBeamAssetID(V6Api& api, const json& params)
    {
        Asset::ID aid = V6Api::getMandatoryParam<PositiveUint32>(params, "asset_id");
        api.checkCAEnabled();
        return aid;
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
                explicit ApiTxStatusInterpreter(const TxParameters& txParams) : TxStatusInterpreter(txParams) {};
                ~ApiTxStatusInterpreter() override = default;

                [[nodiscard]] std::string getStatus() const override
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
                explicit ApiAssetTxStatusInterpreter(const TxParameters& txParams) : AssetTxStatusInterpreter(txParams) {};
                [[nodiscard]] std::string getStatus() const override
                {
                    if (m_status == TxStatus::Registering)
                        return m_selfTx ? "self sending" : (m_sender ? "sending" : "receiving");
                    return AssetTxStatusInterpreter::getStatus();
                }
            };
            statusInterpreter = std::make_unique<AssetTxStatusInterpreter>(tx);
        }
        else if (tx.m_txType == TxType::PushTransaction)
        {
            statusInterpreter = std::make_unique<MaxPrivacyTxStatusInterpreter>(tx);
        }
        else if (tx.m_txType == TxType::AtomicSwap)
        {
#ifdef BEAM_ATOMIC_SWAP_SUPPORT
            statusInterpreter = std::make_unique<SwapTxStatusInterpreter>(tx);
#endif // BEAM_ATOMIC_SWAP_SUPPORT
        }
        else if (tx.m_txType == TxType::Contract)
        {
            statusInterpreter = std::make_unique<ContractTxStatusInterpreter>(tx);
        }
        msg = json
        {
            {"txId", std::to_string(tx.m_txId)},
            {"status", tx.m_status},
            {"status_string", statusInterpreter ? statusInterpreter->getStatus() : "unknown"},
            {"sender", tx.getAddressFrom()},
            {"receiver", tx.getAddressTo()},
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
            beam::bvm2::ContractInvokeData vData;
            if (tx.GetParameter(TxParameterID::ContractDataPacked, vData))
            {
                for (const auto& data : vData)
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

    Amount V6Api::getBeamFeeParam(const json& params, const std::string& name) const
    {
        auto &fs = Transaction::FeeSettings::get(get_TipHeight());
        return getBeamFeeParam(params, name, fs.get_DefaultStd());
    }

    Amount V6Api::getBeamFeeParam(const json& params, const std::string& name, Amount feeMin) const
    {
        auto ofee = getOptionalParam<PositiveUint64>(params, name);
        if (!ofee)
            return feeMin;

        if (*ofee < feeMin)
        {
            std::stringstream ss;
            ss << "Failed to initiate the operation. The minimum fee is " << feeMin << " GROTH.";
            throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, ss.str());
        }
        return *ofee;
    }

    static void FillAddressData(const JsonRpcId& id, const json& params, AddressData& data)
    {
        using namespace beam::wallet;

        if (auto comment = V6Api::getOptionalParam<std::string>(params, "comment"))
        {
            data.comment = *comment;
        }

        if (auto expiration = V6Api::getOptionalParam<NonEmptyString>(params, "expiration"))
        {
            static std::map<std::string, WalletAddress::ExpirationStatus> Items =
            {
                {"expired", WalletAddress::ExpirationStatus::Expired},
                {"24h",     WalletAddress::ExpirationStatus::OneDay},
                {"auto",    WalletAddress::ExpirationStatus::Auto},
                {"never",   WalletAddress::ExpirationStatus::Never},
            };

            if(Items.count(*expiration) == 0)
            {
                throw jsonrpc_exception(ApiError::InvalidJsonRpc, "Unknown value for the 'expiration' parameter.");
            }

            data.expiration = Items[*expiration];
        }
    }

    std::pair<CalcChange, IWalletApi::MethodInfo> V6Api::onParseCalcChange(const JsonRpcId& id, const nlohmann::json& params)
    {
        CalcChange message{ getMandatoryParam<PositiveAmount>(params, "amount") };
        message.assetId = readOptionalAssetID(*this, params);

        if (auto f = getOptionalParam<PositiveUint64>(params, "fee"))
        {
            message.explicitFee = *f;
        }

        if (auto isPush = getOptionalParam<bool>(params, "is_push_transaction"))
        {
            message.isPushTransaction = *isPush;
        }

        return std::make_pair(message, MethodInfo());
    }

    std::pair<ChangePassword, IWalletApi::MethodInfo> V6Api::onParseChangePassword(const JsonRpcId& id, const nlohmann::json& params)
    {
        if (!hasParam(params, "new_pass"))
        {
            throw jsonrpc_exception(ApiError::InvalidJsonRpc, "New password parameter must be specified.");
        }

        ChangePassword message;
        message.newPassword = params["new_pass"].get<std::string>();

        return std::make_pair(message, MethodInfo());
    }

    std::pair<CreateAddress, IWalletApi::MethodInfo> V6Api::onParseCreateAddress(const JsonRpcId& id, const json& params)
    {
        CreateAddress createAddress;
        MethodInfo info;

        beam::wallet::FillAddressData(id, params, createAddress);
        info.comment = createAddress.comment ? *createAddress.comment : std::string();

        const auto otype = getOptionalParam<NonEmptyString>(params, "type");
        if (otype)
        {
            const std::string stype = *otype;
            auto t = std::find_if(_ttypesMap.begin(), _ttypesMap.end(), [&](const auto& p) {
                return p.second == stype;
            });

            if (t != _ttypesMap.end())
            {
                createAddress.type = t->first;
            }
        }

        if (createAddress.type == TokenType::RegularOldStyle)
        {
            if (auto ns = getOptionalParam<bool>(params, "new_style_regular"); ns && *ns)
            {
                createAddress.type = TokenType::RegularNewStyle;
            }
        }

        if (auto opcnt = getOptionalParam<PositiveUint32>(params, "offline_payments"))
        {
            createAddress.offlinePayments = *opcnt;
        }

        return std::make_pair(createAddress, info);
    }

    std::pair<DeleteAddress, IWalletApi::MethodInfo> V6Api::onParseDeleteAddress(const JsonRpcId& id, const json& params)
    {
        DeleteAddress deleteAddress;
        deleteAddress.token = getMandatoryParam<NonEmptyString>(params, "address");

        MethodInfo info;
        info.token = deleteAddress.token;

        return std::make_pair(deleteAddress, info);
    }

    std::pair<EditAddress, IWalletApi::MethodInfo> V6Api::onParseEditAddress(const JsonRpcId& id, const json& params)
    {
        EditAddress editAddress;
        editAddress.token = getMandatoryParam<NonEmptyString>(params, "address");

        if (!hasParam(params, "comment") && !hasParam(params, "expiration"))
        {
            throw jsonrpc_exception(ApiError::InvalidJsonRpc, "Comment or Expiration parameter must be specified.");
        }

        MethodInfo info;
        info.token = editAddress.token;

        beam::wallet::FillAddressData(id, params, editAddress);
        return std::make_pair(editAddress, info);
    }

    std::pair<AddrList, IWalletApi::MethodInfo> V6Api::onParseAddrList(const JsonRpcId& id, const json& params)
    {
        const auto own = getOptionalParam<bool>(params, "own");

        AddrList addrList;
        addrList.own = own && *own;

        return std::pair(addrList, MethodInfo());
    }

    std::pair<ValidateAddress, IWalletApi::MethodInfo> V6Api::onParseValidateAddress(const JsonRpcId& id, const json& params)
    {
        const auto address = getMandatoryParam<NonEmptyString>(params, "address");

        ValidateAddress validateAddress;
        validateAddress.token = address;

        return std::make_pair(validateAddress, MethodInfo());
    }

    std::pair<Send, IWalletApi::MethodInfo> V6Api::onParseSend(const JsonRpcId& id, const json& params)
    {
        MethodInfo info;
        Send send;

        send.tokenTo = getMandatoryParam<NonEmptyString>(params, "address");
        if(auto txParams = ParseParameters(send.tokenTo))
        {
            send.txParameters = std::move(*txParams);
            send.txParameters.SetParameter(beam::wallet::TxParameterID::OriginalToken, send.tokenTo);
            info.token = send.tokenTo;
        }
        else
        {
            throw jsonrpc_exception(ApiError::InvalidAddress , "Invalid receiver address or token.");
        }

        send.addressType = GetAddressType(send.tokenTo);
        if(send.addressType == TxAddressType::Offline)
        {
            // Since v6.0 offline address by default trigger the regular online transaction
            // To trigger an offline tx flag should be provided
            if (auto offline = getOptionalParam<bool>(params, "offline"); !offline || !(*offline))
            {
                send.addressType  = TxAddressType::Regular;
                info.spendOffline = true;
            }
        }
        else if (send.addressType == TxAddressType::PublicOffline)
        {
            info.spendOffline = true;
        }
        else if (send.addressType == TxAddressType::MaxPrivacy)
        {
            info.spendOffline = true;
        }

        send.value = getMandatoryParam<PositiveAmount>(params, "value");
        send.assetId = readOptionalAssetID(*this, params);
        info.appendSpend(send.assetId ? *send.assetId : beam::Asset::s_BeamID, send.value);

        if (hasParam(params, "coins"))
        {
            // TODO: may be no check and optional read
            send.coins = readCoinsParameter(id, params);
        }

        if (auto fromParam = getOptionalParam<NonEmptyString>(params, "from"))
        {
            if(GetAddressType(*fromParam) == TxAddressType::Unknown)
            {
                throw jsonrpc_exception(ApiError::InvalidAddress, "Invalid sender address.");
            }
            send.tokenFrom = fromParam;
        }

        send.fee = getBeamFeeParam(params, "fee");
        info.fee = send.fee;

        if (auto comment = getOptionalParam<std::string>(params, "comment"))
        {
            send.comment = *comment;
            info.comment = *comment;
        }

        info.confirm_title = getOptionalParam<NonEmptyString>(params, "confirm_title");
        info.confirm_comment = getOptionalParam<std::string>(params, "confirm_comment");
        send.txId  = getOptionalParam<ValidTxID>(params, "txId");

        return std::make_pair(send, info);
    }

    std::pair<Status, IWalletApi::MethodInfo> V6Api::onParseStatus(const JsonRpcId& id, const json& params)
    {
        Status status = {};
        status.txId = getMandatoryParam<ValidTxID>(params, "txId");
        return std::make_pair(status, MethodInfo());
    }

    std::pair<Split, IWalletApi::MethodInfo> V6Api::onParseSplit(const JsonRpcId& id, const json& params)
    {
        MethodInfo info;

        Split split;
        split.assetId = readOptionalAssetID(*this, params);

        const json coins = getMandatoryParam<NonEmptyJsonArray>(params, "coins");
        beam::AmountBig::Type splitAmount = 0UL;

        for (const auto& amount: coins)
        {
            if(!amount.is_number_unsigned())
            {
                throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc,"Coin amount must be a 64bit unsigned integer.");
            }

            const auto uamount = amount.get<uint64_t>();
            if (uamount == 0)
            {
                throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc,"Coin amount must be a non-zero 64bit unsigned integer.");
            }

            split.coins.push_back(uamount);
            splitAmount += beam::AmountBig::Type(uamount);
        }

        auto& fs = Transaction::FeeSettings::get(get_TipHeight());

        auto outsCnt = split.coins.size() + 1; // for split result cons + beam change coin (if any)
        if (split.assetId.is_initialized() && *split.assetId != beam::Asset::s_BeamID)
        {
            // asset change coin (if any);
            outsCnt++;
        }

        Amount minimumFee = std::max(fs.m_Kernel + fs.m_Output * outsCnt, fs.get_DefaultStd());

        split.fee = getBeamFeeParam(params, "fee", minimumFee);
        split.txId = getOptionalParam<ValidTxID>(params, "txId");

        auto assetId = split.assetId ? *split.assetId : beam::Asset::s_BeamID;
        info.appendSpend(assetId, splitAmount);
        info.appendSpend(assetId, splitAmount);
        info.fee = split.fee;

        return std::make_pair(split, info);
    }

    std::pair<TxCancel, IWalletApi::MethodInfo> V6Api::onParseTxCancel(const JsonRpcId& id, const json& params)
    {
        TxCancel txCancel = {};
        txCancel.txId = getMandatoryParam<ValidTxID>(params, "txId");
        return std::make_pair(txCancel, MethodInfo());
    }

    std::pair<TxDelete, IWalletApi::MethodInfo> V6Api::onParseTxDelete(const JsonRpcId& id, const json& params)
    {
        TxDelete txDelete = {};
        txDelete.txId = getMandatoryParam<ValidTxID>(params, "txId");
        return std::make_pair(txDelete, MethodInfo());
    }

    std::pair<Issue, IWalletApi::MethodInfo> V6Api::onParseIssue(const JsonRpcId& id, const json& params)
    {
        return onParseIssueConsume<Issue>(true, id, params);
    }

    std::pair<Consume, IWalletApi::MethodInfo> V6Api::onParseConsume(const JsonRpcId& id, const json& params)
    {
        return onParseIssueConsume<Consume>(true, id, params);
    }

    template<typename T>
    std::pair<T, IWalletApi::MethodInfo> V6Api::onParseIssueConsume(bool issue, const JsonRpcId& id, const json& params)
    {
        static_assert(std::is_same<Issue, T>::value || std::is_same<Consume, T>::value);

        T data = {};
        data.value = getMandatoryParam<PositiveUint64>(params, "value");
        data.assetId = readMandatoryNonBeamAssetID(*this, params);

        if (hasParam(params, "coins"))
        {
            data.coins = readCoinsParameter(id, params);
        }

        data.fee = getBeamFeeParam(params, "fee");
        data.txId = getOptionalParam<ValidTxID>(params, "txId");

        MethodInfo info;
        info.fee = data.fee;

        if (issue)
        {
            info.appendReceive(data.assetId, data.value);
        }
        else
        {
            info.appendSpend(data.assetId, data.value);
        }

        return std::make_pair(data, info);
    }

    // template std::pair<Issue, IWalletApi::MethodInfo> V6Api::onParseIssueConsume<Issue>(bool issue, const JsonRpcId& id, const json& params);
    // template std::pair<Consume, IWalletApi::MethodInfo> V6Api::onParseIssueConsume<Consume>(bool issue, const JsonRpcId& id, const json& params);

    std::pair<GetAssetInfo, IWalletApi::MethodInfo> V6Api::onParseGetAssetInfo(const JsonRpcId& id, const json& params)
    {
        GetAssetInfo data = {0};
        data.assetId = readMandatoryNonBeamAssetID(*this, params);
        return std::make_pair(data, MethodInfo());
    }

    std::pair<SetConfirmationsCount, IWalletApi::MethodInfo> V6Api::onParseSetConfirmationsCount(const JsonRpcId& id, const json& params)
    {
        SetConfirmationsCount data;
        data.count = getMandatoryParam<uint32_t>(params, "count");
        return std::make_pair(data, MethodInfo());
    }

    std::pair<GetConfirmationsCount, IWalletApi::MethodInfo> V6Api::onParseGetConfirmationsCount(const JsonRpcId& id, const json& params)
    {
        GetConfirmationsCount data;
        return std::make_pair(data, MethodInfo());
    }

    std::pair<TxAssetInfo, IWalletApi::MethodInfo> V6Api::onParseTxAssetInfo(const JsonRpcId& id, const json& params)
    {
        TxAssetInfo data = {0};

        data.assetId = readMandatoryNonBeamAssetID(*this, params);
        data.txId = getOptionalParam<ValidTxID>(params, "txId");

        return std::make_pair(data, MethodInfo());
    }

    std::pair<GetUtxo, IWalletApi::MethodInfo> V6Api::onParseGetUtxo(const JsonRpcId& id, const json& params)
    {
        GetUtxo getUtxo;

        if (auto count = getOptionalParam<PositiveUint32>(params, "count"))
        {
            getUtxo.count = *count;
        }

        if (hasParam(params, "filter"))
        {
            getUtxo.filter.assetId = readOptionalAssetID(*this, params["filter"]);
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

        return std::make_pair(getUtxo, MethodInfo());
    }

    std::pair<TxList, IWalletApi::MethodInfo> V6Api::onParseTxList(const JsonRpcId& id, const json& params)
    {
        TxList txList;

        if (hasParam(params, "filter"))
        {
            if (hasParam(params["filter"], "status") && params["filter"]["status"].is_number_unsigned())
            {
                txList.filter.status = (TxStatus)params["filter"]["status"];
            }

            if (hasParam(params["filter"], "height") && params["filter"]["height"].is_number_unsigned())
            {
                txList.filter.height = (Height)params["filter"]["height"];
            }

            txList.filter.assetId = readOptionalAssetID(*this, params["filter"]);
        }

        if (auto count = getOptionalParam<PositiveUint32>(params, "count"))
        {
            txList.count = *count;
        }

        if (auto skip = getOptionalParam<uint32_t>(params, "skip"))
        {
            txList.skip = *skip;
        }

        return std::make_pair(txList, MethodInfo());
    }

    std::pair<WalletStatusApi, IWalletApi::MethodInfo> V6Api::onParseWalletStatusApi(const JsonRpcId& id, const json& params)
    {
        WalletStatusApi walletStatus;
        return std::make_pair(walletStatus, MethodInfo());
    }

    std::pair<GenerateTxId, IWalletApi::MethodInfo> V6Api::onParseGenerateTxId(const JsonRpcId& id, const json& params)
    {
        GenerateTxId generateTxId;
        return std::make_pair(generateTxId, MethodInfo());
    }

    std::pair<ExportPaymentProof, IWalletApi::MethodInfo> V6Api::onParseExportPaymentProof(const JsonRpcId& id, const json& params)
    {
        ExportPaymentProof data = {};
        data.txId = getMandatoryParam<ValidTxID>(params, "txId");
        return std::make_pair(data, MethodInfo());
    }

    std::pair<VerifyPaymentProof, IWalletApi::MethodInfo> V6Api::onParseVerifyPaymentProof(const JsonRpcId& id, const json& params)
    {
        VerifyPaymentProof data;

        const auto proof = getMandatoryParam<NonEmptyString>(params, "payment_proof");
        data.paymentProof = from_hex(proof);

        return std::make_pair(data, MethodInfo());
    }

    std::pair<InvokeContract, IWalletApi::MethodInfo> V6Api::onParseInvokeContract(const JsonRpcId &id, const json &params)
    {
        InvokeContract message;

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

        return std::make_pair(message, MethodInfo());
    }

    std::pair<ProcessInvokeData, IWalletApi::MethodInfo> V6Api::onParseProcessInvokeData(const JsonRpcId &id, const json& params)
    {
        ProcessInvokeData message;

        const json bytes = getMandatoryParam<NonEmptyJsonArray>(params, "data");
        message.invokeData = bytes.get<std::vector<uint8_t>>();

        beam::bvm2::ContractInvokeData realData;

        try
        {
            if (!beam::wallet::fromByteBuffer(message.invokeData, realData))
            {
                throw std::runtime_error("");
            }
        }
        catch(std::runtime_error&)
        {
            throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, "Failed to parse invoke data");
        }

        LOG_INFO() << "onParseProcessInvokeData";
        for (const auto& entry: realData)
        {
            LOG_INFO() << "\tCid: "     << entry.m_Cid;
            LOG_INFO() << "\tMethod: "  << entry.m_iMethod;
            LOG_INFO() << "\tCharge: " << entry.m_Charge;
            LOG_INFO() << "\tComment: " << entry.m_sComment;

            for (const auto& spend: entry.m_Spend)
            {
                LOG_INFO() << "\t (aid, amount): (" << spend.first << ", " << spend.second << ")";
            }
        }

        MethodInfo info;
        info.spendOffline = false;
        info.comment = beam::bvm2::getFullComment(realData);
        info.fee = beam::bvm2::getFullFee(realData, getWallet()->get_TipHeight());
        info.confirm_title = getOptionalParam<NonEmptyString>(params, "confirm_title");
        info.confirm_comment = getOptionalParam<std::string>(params, "confirm_comment");

        const auto fullSpend = beam::bvm2::getFullSpend(realData);
        for (const auto& spend: fullSpend)
        {
            if (spend.second < 0)
            {
                Amount amount = std::abs(spend.second);
                info.appendReceive(spend.first, beam::AmountBig::Type(amount));
            }
            else
            {
                Amount amount = spend.second;
                info.appendSpend(spend.first, beam::AmountBig::Type(amount));
            }
        }

        return std::make_pair(message, info);
    }

    std::pair<BlockDetails, IWalletApi::MethodInfo> V6Api::onParseBlockDetails(const JsonRpcId& id, const json& params)
    {
        BlockDetails message = {0};

        const auto height = getMandatoryParam<Height>(params, "height");
        message.blockHeight = height;

        return std::make_pair(message, MethodInfo());
    }

    void V6Api::getResponse(const JsonRpcId& id, const CalcChange::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result",
                {
                    {"change", res.change},
                    {"change_str", std::to_string(res.change)}, // string representation
                    {"asset_change", res.assetChange},
                    {"asset_change_str", std::to_string(res.assetChange)}, // string representation
                    {"explicit_fee", res.explicitFee},
                    {"explicit_fee_str", std::to_string(res.explicitFee)} // string representation
                }
            }
        };
    }

    void V6Api::getResponse(const JsonRpcId& id, const ChangePassword::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", "done"}
        };
    }

    void V6Api::getResponse(const JsonRpcId& id, const CreateAddress::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", res.token}
        };
    }

    void V6Api::getResponse(const JsonRpcId& id, const DeleteAddress::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", "done"}
        };
    }

    void V6Api::getResponse(const JsonRpcId& id, const ProcessInvokeData::Response& res, json& msg)
    {
        msg = nlohmann::json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result",
                {
                    {"txid", std::to_string(res.txid)}
                }
            }
        };
    }

    void V6Api::getResponse(const JsonRpcId& id, const InvokeContract::Response& res, json& msg)
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

    void V6Api::getResponse(const JsonRpcId& id, const BlockDetails::Response& res, json& msg)
    {
        msg = nlohmann::json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result",
                {
                    {"height", res.height},
                    {"block_hash", res.blockHash},
                    {"previous_block", res.previousBlock},
                    {"chainwork", res.chainwork},
                    {"kernels", res.kernels},
                    {"definition", res.definition},
                    {"timestamp", res.timestamp},
                    {"pow", res.pow},
                    {"difficulty", res.difficulty},
                    {"packed_difficulty", res.packedDifficulty},
                    {"rules_hash", res.rulesHash}
                }
            }
        };
    }

    void V6Api::getResponse(const JsonRpcId& id, const EditAddress::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", "done"}
        };
    }

    void V6Api::fillAddresses(json& parent, const std::vector<WalletAddress>& items)
    {
        for (auto& addr : items)
        {
            auto type = GetTokenType(addr.m_Address);
            json obj = {
                {"address",     addr.m_Address},
                {"comment",     addr.m_label},
                {"category",    addr.m_category},
                {"create_time", addr.getCreateTime()},
                {"duration",    addr.m_duration},
                {"expired",     addr.isExpired()},
                {"own",         addr.isOwn()},
                {"own_id",      addr.m_OwnID},
                {"own_id_str",  std::to_string(addr.m_OwnID)},
                {"wallet_id",   std::to_string(addr.m_walletID)},
                {"type",        getTokenType(type)}
            };

            if (addr.m_Identity != Zero)
            {
                obj["identity"] = std::to_string(addr.m_Identity);
            }

            parent.push_back(obj);
        }
    }

    void V6Api::getResponse(const JsonRpcId& id, const AddrList::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", json::array()}
        };

        fillAddresses(msg["result"], res.list);
    }

    void V6Api::getResponse(const JsonRpcId& id, const ValidateAddress::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result",
                {
                    {"is_valid",  res.isValid},
                    {"is_mine",   res.isMine},
                    {"type",      getTokenType(res.type)}
                }
            }
        };
        if (res.payments)
        {
            msg["result"]["payments"] = *res.payments;
        }
    }

    void V6Api::fillCoins(json& arr, const std::vector<ApiCoin>& coins)
    {
        for (auto& c : coins)
        {
            #define MACRO(name, type) {#name, c.name},
            arr.push_back({
                BEAM_GET_UTXO_RESPONSE_FIELDS(MACRO)
            });
            #undef MACRO
        }
    }

    void V6Api::getResponse(const JsonRpcId& id, const GetUtxo::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", json::array()}
        };

        fillCoins(msg["result"], res.coins);
    }

    void V6Api::getResponse(const JsonRpcId& id, const Send::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result",
                {
                    {"txId", std::to_string(res.txId)}
                }
            }
        };
    }

    void V6Api::getResponse(const JsonRpcId& id, const Issue::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result",
                {
                    {"txId", std::to_string(res.txId)}
                }
            }
        };
    }

    void V6Api::getResponse(const JsonRpcId& id, const TxAssetInfo::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result",
                {
                    {"txId", std::to_string(res.txId)}
                }
            }
        };
    }

    void V6Api::fillAssetInfo(json& res, const WalletAsset& info)
    {
        std::string strMeta;
        info.m_Metadata.get_String(strMeta);

        res["asset_id"]      = info.m_ID;
        res["lockHeight"]    = info.m_LockHeight;
        res["refreshHeight"] = info.m_RefreshHeight;
        res["ownerId"]       = info.m_Owner.str();
        res["isOwned"]       = info.m_IsOwned;
        res["metadata"]      = strMeta;
        res["emission_str"]  = std::to_string(info.m_Value);

        if(info.m_Value <= kMaxAllowedInt)
        {
            res["emission"] = AmountBig::get_Lo(info.m_Value);
        }
    }

    void V6Api::getResponse(const JsonRpcId &id, const GetAssetInfo::Response &res, json &msg)
    {
        std::string strMeta;
        res.AssetInfo.m_Metadata.get_String(strMeta);

        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", json::object()}
        };

        fillAssetInfo(msg["result"], res.AssetInfo);
    }

    void V6Api::getResponse(const JsonRpcId &id, const SetConfirmationsCount::Response &res, json &msg)
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

    void V6Api::getResponse(const JsonRpcId &id, const GetConfirmationsCount::Response &res, json &msg)
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

    void V6Api::getResponse(const JsonRpcId& id, const Consume::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result",
                {
                    {"txId", std::to_string(res.txId)}
                }
            }
        };
    }

    void V6Api::getResponse(const JsonRpcId& id, const Status::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", {}}
        };

        GetStatusResponseJson(res.tx, msg["result"], res.txHeight, res.systemHeight, true);
    }

    void V6Api::getResponse(const JsonRpcId& id, const Split::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result",
                {
                    {"txId", std::to_string(res.txId)}
                }
            }
        };
    }

    void V6Api::getResponse(const JsonRpcId& id, const TxCancel::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", res.result}
        };
    }

    void V6Api::getResponse(const JsonRpcId& id, const TxDelete::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", res.result}
        };
    }

    void V6Api::fillTransactions(json& arr, const std::vector<Status::Response> txs)
    {
        for (const auto& resItem : txs)
        {
            json item = {};
            GetStatusResponseJson(
                resItem.tx,
                item,
                resItem.txHeight,
                resItem.systemHeight,
                true);
            arr.push_back(item);
        }
    }

    void V6Api::getResponse(const JsonRpcId& id, const TxList::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", json::array()}
        };

        fillTransactions(msg["result"], res.resultList);
    }

    void V6Api::getResponse(const JsonRpcId& id, const WalletStatusApi::Response& res, json& msg)
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

    void V6Api::getResponse(const JsonRpcId& id, const GenerateTxId::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", std::to_string(res.txId)}
        };
    }

    void V6Api::getResponse(const JsonRpcId& id, const ExportPaymentProof::Response& res, json& msg)
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

    void V6Api::getResponse(const JsonRpcId& id, const VerifyPaymentProof::Response& res, json& msg)
    {
        auto f = [&id](auto& pi)
        {
            return json
            {
                {JsonRpcHeader, JsonRpcVersion},
                {"id", id},
                {"result",
                    {
                        {"is_valid", pi.IsValid()},

                        {"sender", std::to_string(pi.m_Sender)},
                        {"receiver", std::to_string(pi.m_Receiver)},
                        {"amount", pi.m_Amount},
                        {"kernel", std::to_string(pi.m_KernelID)},
                        {"asset_id", pi.m_AssetID}
                        //{"signature", std::to_string(res.paymentInfo.m_Signature)}
                    }
                }
            };
        };
        if (res.paymentInfo)
        {
            msg = f(*res.paymentInfo);
        }
        else if (res.shieldedPaymentInfo)
        {
            msg = f(*res.shieldedPaymentInfo);
        }
    }
}
