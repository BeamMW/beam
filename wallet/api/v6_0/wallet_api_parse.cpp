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
            {"txId", TxIDToString(tx.m_txId)},
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

    WalletApi::WalletApi(
            IWalletApiHandler& handler,
            ACL acl,
            IWalletDB::Ptr wdb,
            Wallet::Ptr wallet,
            ISwapsProvider::Ptr swaps,
            IShadersManager::Ptr contracts
        )
        : ApiBase(handler, std::move(acl))
        , _wdb(std::move(wdb))
        , _wallet(std::move(wallet))
        , _swaps(std::move(swaps))
        , _contracts(std::move(contracts))
    {
        #define REG_FUNC(api, name, writeAccess, isAsync) \
        _methods[name] = {BIND_THIS_MEMFN(on##api##Message), writeAccess, isAsync};
        WALLET_API_METHODS(REG_FUNC)
        #undef REG_FUNC

        #define REG_ALIASES_FUNC(aliasName, api, name, writeAccess) \
        _methods[aliasName] = {BIND_THIS_MEMFN(on##api##Message), writeAccess};
        WALLET_API_METHODS_ALIASES(REG_ALIASES_FUNC)
        #undef REG_ALIASES_FUNC
        //WALLET_API_METHODS_ALIASES
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
        if (auto comment = WalletApi::getOptionalParam<std::string>(params, "comment"))
        {
            data.comment = *comment;
        }

        if (auto expiration = WalletApi::getOptionalParam<NonEmptyString>(params, "expiration"))
        {
            static std::map<std::string, AddressData::Expiration> Items =
            {
                {"expired", AddressData::Expired},
                {"24h",     AddressData::Auto}, // just not to break API, map 24h to auto
                {"auto",    AddressData::Auto},
                {"never",   AddressData::Never},
            };

            if(Items.count(*expiration) == 0)
            {
                throw jsonrpc_exception(ApiError::InvalidJsonRpc, "Unknown value for the 'expiration' parameter.");
            }

            data.expiration = Items[*expiration];
        }
    }

    void WalletApi::onCalcMyChangeMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        CalcMyChange message{ getMandatoryParam<PositiveAmount>(params, "amount") };
        onMessage(id, message);
    }

    void WalletApi::onChangePasswordMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        if (!hasParam(params, "new_pass"))
        {
            throw jsonrpc_exception(ApiError::InvalidJsonRpc, "New password parameter must be specified.");
        }
        ChangePassword message;
        message.newPassword = params["new_pass"].get<std::string>();
        onMessage(id, message);
    }

    void WalletApi::onCreateAddressMessage(const JsonRpcId& id, const json& params)
    {
        CreateAddress createAddress;
        beam::wallet::FillAddressData(id, params, createAddress);
        auto it = params.find("type");
        if (it != params.end())
        {
            static constexpr std::array<std::pair<std::string_view, TxAddressType>, 4> types =
            {
                {
                    {"regular",         TxAddressType::Regular},
                    {"offline",         TxAddressType::Offline},
                    {"max_privacy",     TxAddressType::MaxPrivacy},
                    {"public_offline",  TxAddressType::PublicOffline}
                }
            };
            auto t = std::find_if(types.begin(), types.end(), [&](const auto& p) { return p.first == it->get<std::string>(); });
            if (t != types.end())
            {
                createAddress.type = t->second;
            }
        }
        it = params.find("new_style_regular");
        if (it != params.end())
        {
            createAddress.newStyleRegular = it->get<bool>();
        }
        it = params.find("offline_payments");
        if (it != params.end())
        {
            createAddress.offlinePayments = it->get<uint32_t>();
        }
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
            send.txParameters = std::move(*txParams);
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

        const json coins = getMandatoryParam<NonEmptyJsonArray>(params, "coins");
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
        }

        auto& fs = Transaction::FeeSettings::get(get_CurrentHeight());
        Amount minimumFee = std::max(fs.m_Kernel + fs.m_Output * (split.coins.size() + 1), fs.get_DefaultStd());

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
        data.fee = getBeamFeeParam(params, "fee");
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

    /*
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
















#endif  // BEAM_ATOMIC_SWAP_SUPPORT
*/
    void WalletApi::onInvokeContractMessage(const JsonRpcId &id, const json &params)
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

        onMessage(id, message);
    }

    void WalletApi::getResponse(const JsonRpcId& id, const CalcMyChange::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result",
                {
                    {"change", res.change},
                    {"change_str", std::to_string(res.change)} // string representation
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
            msg["result"]["txid"] = TxIDToString(res.txid);
        }
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
}
