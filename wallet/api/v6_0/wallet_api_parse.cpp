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
#include "wallet_api.h"
#include "wallet/core/common_utils.h"
#include "utility/fsutils.h"
#include "bvm/ManagerStd.h"
#include "wallet/transactions/swaps/swap_tx_description.h"

namespace beam::wallet
{
    namespace
    {
        // This is for jscript compatibility
        // Number.MAX_SAFE_INTEGER
        const auto kMaxAllowedInt = AmountBig::Type(9'007'199'254'740'991U);
    }

    CoinIDList readCoinsParameter(const JsonRpcId& id, const json& params)
    {
        CoinIDList coins;

        if (!params["coins"].is_array() || params["coins"].empty())
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

    bool readAssetsParameter(const json& params)
    {
        if (auto oassets = WalletApi::getOptionalParam<bool>(params, "assets"))
        {
            return *oassets;
        }
        return false;
    }

    boost::optional<Asset::ID> readOptionalAssetID(const json& params)
    {
        auto aid = WalletApi::getOptionalParam<uint32_t>(params, "asset_id");
        if (aid && *aid != Asset::s_InvalidID)
        {
            WalletApi::checkCAEnabled();
        }
        return aid;
    }

    Asset::ID readMandatoryNonBeamAssetID(const json& params)
    {
        Asset::ID aid = WalletApi::getMandatoryParam<PositiveUint32>(params, "asset_id");
        WalletApi::checkCAEnabled();
        return aid;
    }

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
        else if (tx.m_txType == TxType::PushTransaction)
        {
            statusInterpreter = std::make_unique<MaxPrivacyTxStatusInterpreter>(tx);
        }
        else if (tx.m_txType == TxType::AtomicSwap)
        {
            statusInterpreter = std::make_unique<SwapTxStatusInterpreter>(tx);
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
            std::vector<beam::bvm2::ContractInvokeData> vData;
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

    Amount WalletApi::getBeamFeeParam(const json& params, const std::string& name) const
    {
        auto& fs = Transaction::FeeSettings::get(get_CurrentHeight());
        return getBeamFeeParam(params, name, fs.get_DefaultStd());
    }

    Amount WalletApi::getBeamFeeParam(const json& params, const std::string& name, Amount feeMin) const
    {
        auto ofee = getOptionalParam<PositiveUnit64>(params, name);
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

        if (auto comment = WalletApi::getOptionalParam<std::string>(params, "comment"))
        {
            data.comment = *comment;
        }

        if (auto expiration = WalletApi::getOptionalParam<NonEmptyString>(params, "expiration"))
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

    std::pair<CalcChange, IWalletApi::MethodInfo> WalletApi::onParseCalcChange(const JsonRpcId& id, const nlohmann::json& params)
    {
        CalcChange message{ getMandatoryParam<PositiveAmount>(params, "amount") };
        message.assetId = readOptionalAssetID(params);

        if (auto f = getOptionalParam<PositiveUnit64>(params, "fee"))
        {
            message.explicitFee = *f;
        }

        if (auto isPush = getOptionalParam<bool>(params, "is_push_transaction"))
        {
            message.isPushTransaction = *isPush;
        }

        return std::make_pair(message, MethodInfo());
    }

    std::pair<ChangePassword, IWalletApi::MethodInfo> WalletApi::onParseChangePassword(const JsonRpcId& id, const nlohmann::json& params)
    {
        if (!hasParam(params, "new_pass"))
        {
            throw jsonrpc_exception(ApiError::InvalidJsonRpc, "New password parameter must be specified.");
        }

        ChangePassword message;
        message.newPassword = params["new_pass"].get<std::string>();

        return std::make_pair(message, MethodInfo());
    }

    std::pair<CreateAddress, IWalletApi::MethodInfo> WalletApi::onParseCreateAddress(const JsonRpcId& id, const json& params)
    {
        CreateAddress createAddress;
        MethodInfo info;

        beam::wallet::FillAddressData(id, params, createAddress);
        info.comment = createAddress.comment ? *createAddress.comment : std::string();

        auto it = params.find("type");
        if (it != params.end())
        {
            static constexpr std::array<std::pair<std::string_view, TokenType>, 6> types =
            {
                {
                    {"regular",         TokenType::RegularOldStyle},
                    {"offline",         TokenType::Offline},
                    {"max_privacy",     TokenType::MaxPrivacy},
                    {"public_offline",  TokenType::Public},
                    {"regular_new",     TokenType::RegularNewStyle}
                }
            };
            auto t = std::find_if(types.begin(), types.end(), [&](const auto& p) { return p.first == it->get<std::string>(); });
            if (t != types.end())
            {
                createAddress.type = t->second;
            }
        }

        if (createAddress.type == TokenType::RegularOldStyle)
        {
            it = params.find("new_style_regular");
            if (it != params.end())
            {
                if (it->get<bool>())
                {
                    createAddress.type = TokenType::RegularNewStyle;
                }
            }
        }

        it = params.find("offline_payments");
        if (it != params.end())
        {
            createAddress.offlinePayments = it->get<uint32_t>();
        }

        return std::make_pair(createAddress, info);
    }

    std::pair<DeleteAddress, IWalletApi::MethodInfo> WalletApi::onParseDeleteAddress(const JsonRpcId& id, const json& params)
    {
        const auto address = getMandatoryParam<NonEmptyString>(params, "address");

        DeleteAddress deleteAddress;
        deleteAddress.address.FromHex(address);

        MethodInfo info;
        info.token = address;

        return std::make_pair(deleteAddress, info);
    }

    std::pair<EditAddress, IWalletApi::MethodInfo> WalletApi::onParseEditAddress(const JsonRpcId& id, const json& params)
    {
        const auto address = getMandatoryParam<NonEmptyString>(params, "address");

        if (!hasParam(params, "comment") && !hasParam(params, "expiration"))
        {
            throw jsonrpc_exception(ApiError::InvalidJsonRpc, "Comment or Expiration parameter must be specified.");
        }

        EditAddress editAddress;
        editAddress.address.FromHex(address);

        MethodInfo info;
        info.token = address;

        beam::wallet::FillAddressData(id, params, editAddress);
        return std::make_pair(editAddress, info);
    }

    std::pair<AddrList, IWalletApi::MethodInfo> WalletApi::onParseAddrList(const JsonRpcId& id, const json& params)
    {
        const auto own = getOptionalParam<bool>(params, "own");

        AddrList addrList = {};
        addrList.own = own && *own;

        return std::pair(addrList, MethodInfo());
    }

    std::pair<ValidateAddress, IWalletApi::MethodInfo> WalletApi::onParseValidateAddress(const JsonRpcId& id, const json& params)
    {
        const auto address = getMandatoryParam<NonEmptyString>(params, "address");

        ValidateAddress validateAddress;
        validateAddress.address = address;

        return std::make_pair(validateAddress, MethodInfo());
    }

    std::pair<Send, IWalletApi::MethodInfo> WalletApi::onParseSend(const JsonRpcId& id, const json& params)
    {
        MethodInfo info;
        Send send;

        const std::string addressOrToken = getMandatoryParam<NonEmptyString>(params, "address");
        if(auto txParams = ParseParameters(addressOrToken))
        {
            send.txParameters = std::move(*txParams);
            info.token = addressOrToken;
        }
        else
        {
            throw jsonrpc_exception(ApiError::InvalidAddress , "Invalid receiver address or token.");
        }

        send.addressType = GetAddressType(addressOrToken);
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
        send.assetId = readOptionalAssetID(params);
        info.spend[send.assetId ? *send.assetId : beam::Asset::s_BeamID] = send.value;

        if (hasParam(params, "coins"))
        {
            // TODO: may be no check and optional read
            send.coins = readCoinsParameter(id, params);
        }

        auto peerID = send.txParameters.GetParameter<WalletID>(TxParameterID::PeerID);
        if (peerID)
        {
            send.address = *peerID;
        }
        if (!peerID || std::to_string(*peerID) != addressOrToken)
        {
            send.txParameters.SetParameter(beam::wallet::TxParameterID::OriginalToken, addressOrToken);
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

        send.fee = getBeamFeeParam(params, "fee");
        info.fee = send.fee;

        if (auto comment = getOptionalParam<std::string>(params, "comment"))
        {
            send.comment = *comment;
            info.comment = *comment;
        }

        send.txId = getOptionalParam<ValidTxID>(params, "txId");
        return std::make_pair(send, info);
    }

    std::pair<Status, IWalletApi::MethodInfo> WalletApi::onParseStatus(const JsonRpcId& id, const json& params)
    {
        Status status = {};
        status.txId = getMandatoryParam<ValidTxID>(params, "txId");
        return std::make_pair(status, MethodInfo());
    }

    std::pair<Split, IWalletApi::MethodInfo> WalletApi::onParseSplit(const JsonRpcId& id, const json& params)
    {
        MethodInfo info;

        Split split;
        split.assetId = readOptionalAssetID(params);

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

        auto& fs = Transaction::FeeSettings::get(get_CurrentHeight());
        Amount minimumFee = std::max(fs.m_Kernel + fs.m_Output * (split.coins.size() + 1), fs.get_DefaultStd());

        split.fee = getBeamFeeParam(params, "fee", minimumFee);
        split.txId = getOptionalParam<ValidTxID>(params, "txId");

        auto assetId = split.assetId ? *split.assetId : beam::Asset::s_BeamID;
        info.spend[assetId] = splitAmount;
        info.receive[assetId] = splitAmount;
        info.fee = split.fee;

        return std::make_pair(split, info);
    }

    std::pair<TxCancel, IWalletApi::MethodInfo> WalletApi::onParseTxCancel(const JsonRpcId& id, const json& params)
    {
        TxCancel txCancel = {};
        txCancel.txId = getMandatoryParam<ValidTxID>(params, "txId");
        return std::make_pair(txCancel, MethodInfo());
    }

    std::pair<TxDelete, IWalletApi::MethodInfo> WalletApi::onParseTxDelete(const JsonRpcId& id, const json& params)
    {
        TxDelete txDelete = {};
        txDelete.txId = getMandatoryParam<ValidTxID>(params, "txId");
        return std::make_pair(txDelete, MethodInfo());
    }

    std::pair<Issue, IWalletApi::MethodInfo> WalletApi::onParseIssue(const JsonRpcId& id, const json& params)
    {
        return onParseIssueConsume<Issue>(true, id, params);
    }

    std::pair<Consume, IWalletApi::MethodInfo> WalletApi::onParseConsume(const JsonRpcId& id, const json& params)
    {
        return onParseIssueConsume<Consume>(true, id, params);
    }

    template<typename T>
    std::pair<T, IWalletApi::MethodInfo> WalletApi::onParseIssueConsume(bool issue, const JsonRpcId& id, const json& params)
    {
        T data;
        data.value = getMandatoryParam<PositiveUnit64>(params, "value");
        data.assetId = readMandatoryNonBeamAssetID(params);

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
            info.receive[data.assetId] = data.value;
        }
        else
        {
            info.spend[data.assetId] = data.value;
        }

        return std::make_pair(data, info);
    }

    template std::pair<Issue, IWalletApi::MethodInfo> WalletApi::onParseIssueConsume<Issue>(bool issue, const JsonRpcId& id, const json& params);
    template std::pair<Consume, IWalletApi::MethodInfo> WalletApi::onParseIssueConsume<Consume>(bool issue, const JsonRpcId& id, const json& params);

    std::pair<GetAssetInfo, IWalletApi::MethodInfo> WalletApi::onParseGetAssetInfo(const JsonRpcId& id, const json& params)
    {
        GetAssetInfo data = {0};
        data.assetId = readMandatoryNonBeamAssetID(params);
        return std::make_pair(data, MethodInfo());
    }

    std::pair<SetConfirmationsCount, IWalletApi::MethodInfo> WalletApi::onParseSetConfirmationsCount(const JsonRpcId& id, const json& params)
    {
        SetConfirmationsCount data;
        data.count = getMandatoryParam<uint32_t>(params, "count");
        return std::make_pair(data, MethodInfo());
    }

    std::pair<GetConfirmationsCount, IWalletApi::MethodInfo> WalletApi::onParseGetConfirmationsCount(const JsonRpcId& id, const json& params)
    {
        GetConfirmationsCount data;
        return std::make_pair(data, MethodInfo());
    }

    std::pair<TxAssetInfo, IWalletApi::MethodInfo> WalletApi::onParseTxAssetInfo(const JsonRpcId& id, const json& params)
    {
        TxAssetInfo data = {0};

        data.assetId = readMandatoryNonBeamAssetID(params);
        data.txId = getOptionalParam<ValidTxID>(params, "txId");

        return std::make_pair(data, MethodInfo());
    }

    std::pair<GetUtxo, IWalletApi::MethodInfo> WalletApi::onParseGetUtxo(const JsonRpcId& id, const json& params)
    {
        GetUtxo getUtxo;
        getUtxo.withAssets = readAssetsParameter(params);

        if (auto count = getOptionalParam<PositiveUint32>(params, "count"))
        {
            getUtxo.count = *count;
        }

        if (hasParam(params, "filter"))
        {
            getUtxo.filter.assetId = readOptionalAssetID(params["filter"]);
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

    std::pair<TxList, IWalletApi::MethodInfo> WalletApi::onParseTxList(const JsonRpcId& id, const json& params)
    {
        TxList txList;
        txList.withAssets = readAssetsParameter(params);

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

            txList.filter.assetId = readOptionalAssetID(params["filter"]);
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

    std::pair<WalletStatusApi, IWalletApi::MethodInfo> WalletApi::onParseWalletStatusApi(const JsonRpcId& id, const json& params)
    {
        WalletStatusApi walletStatus;
        walletStatus.withAssets = readAssetsParameter(params);
        return std::make_pair(walletStatus, MethodInfo());
    }

    std::pair<GenerateTxId, IWalletApi::MethodInfo> WalletApi::onParseGenerateTxId(const JsonRpcId& id, const json& params)
    {
        GenerateTxId generateTxId;
        return std::make_pair(generateTxId, MethodInfo());
    }

    std::pair<ExportPaymentProof, IWalletApi::MethodInfo> WalletApi::onParseExportPaymentProof(const JsonRpcId& id, const json& params)
    {
        ExportPaymentProof data = {};
        data.txId = getMandatoryParam<ValidTxID>(params, "txId");
        return std::make_pair(data, MethodInfo());
    }

    std::pair<VerifyPaymentProof, IWalletApi::MethodInfo> WalletApi::onParseVerifyPaymentProof(const JsonRpcId& id, const json& params)
    {
        VerifyPaymentProof data;

        const auto proof = getMandatoryParam<NonEmptyString>(params, "payment_proof");
        data.paymentProof = from_hex(proof);

        return std::make_pair(data, MethodInfo());
    }

    std::pair<InvokeContract, IWalletApi::MethodInfo> WalletApi::onParseInvokeContract(const JsonRpcId &id, const json &params)
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

        return std::make_pair(message, MethodInfo());
    }

    std::pair<BlockDetails, IWalletApi::MethodInfo> WalletApi::onParseBlockDetails(const JsonRpcId& id, const json& params)
    {
        BlockDetails message = {0};

        const auto height = getMandatoryParam<Height>(params, "height");
        message.blockHeight = height;

        return std::make_pair(message, MethodInfo());
    }

    void WalletApi::getResponse(const JsonRpcId& id, const CalcChange::Response& res, json& msg)
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

    void WalletApi::getResponse(const JsonRpcId& id, const ChangePassword::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", "done"}
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const CreateAddress::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", res.address}
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
                    {"output", res.output}
                }
            }
        };

        if (res.txid != TxID())
        {
            msg["result"]["txid"] = std::to_string(res.txid);
        }
    }

    void WalletApi::getResponse(const JsonRpcId& id, const BlockDetails::Response& res, json& msg)
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
            std::string createTxId = utxo.m_createTxId.is_initialized() ? std::to_string(*utxo.m_createTxId) : "";
            std::string spentTxId = utxo.m_spentTxId.is_initialized() ? std::to_string(*utxo.m_spentTxId) : "";

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
                {"status_string", utxo.getStatusString()}
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
                    {"txId", std::to_string(res.txId)}
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
                    {"txId", std::to_string(res.txId)}
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
                    {"txId", std::to_string(res.txId)}
                }
            }
        };
    }

    void WalletApi::getResponse(const JsonRpcId &id, const GetAssetInfo::Response &res, json &msg)
    {
        std::string strMeta;
        res.AssetInfo.m_Metadata.get_String(strMeta);

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
        if(res.AssetInfo.m_Value <= kMaxAllowedInt)
        {
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
                    {"txId", std::to_string(res.txId)}
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
                    {"txId", std::to_string(res.txId)}
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
                resItem.systemHeight,
                true);
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

                if (totals.Avail <= kMaxAllowedInt) {
                    jtotals["available"] = AmountBig::get_Lo(totals.Avail);
                }
                jtotals["available_str"] = std::to_string(totals.Avail);

                if (totals.Incoming <= kMaxAllowedInt) {
                    jtotals["receiving"] = AmountBig::get_Lo(totals.Incoming);
                }
                jtotals["receiving_str"] = std::to_string(totals.Incoming);

                if (totals.Outgoing <= kMaxAllowedInt) {
                    jtotals["sending"] = AmountBig::get_Lo(totals.Outgoing);
                }
                jtotals["sending_str"] = std::to_string(totals.Outgoing);

                if (totals.Maturing <= kMaxAllowedInt) {
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
            {"result", std::to_string(res.txId)}
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
}
