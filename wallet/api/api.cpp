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

#include "wallet/api/api.h"
#include "wallet/core/common_utils.h"
#ifdef BEAM_ATOMIC_SWAP_SUPPORT
#include "wallet/transactions/swaps/swap_offer_token.h"
#include "wallet/transactions/swaps/bridges/bitcoin/bitcoin_side.h"
#include "wallet/transactions/swaps/bridges/litecoin/litecoin_side.h"
#include "wallet/transactions/swaps/bridges/qtum/qtum_side.h"
#include "wallet/transactions/swaps/utils.h"
#endif  // BEAM_ATOMIC_SWAP_SUPPORT

#include "nlohmann/json.hpp"

using json = nlohmann::json;

namespace beam::wallet
{    
namespace
{
static const char JsonRpcHrd[] = "jsonrpc";
static const char JsonRpcVerHrd[] = "2.0";

struct jsonrpc_exception
{
    ApiError code;
    std::string data;
    JsonRpcId id;
};

static json getNotImplError(const JsonRpcId& id)
{
    return json
    {
        {JsonRpcHrd, JsonRpcVerHrd},
        {"id", id},
        {"error",
            {
                {"code", ApiError::InternalErrorJsonRpc},
                {"message", "Not implemented yet!"},
            }
        }
    };
}

std::string getJsonString(const char* data, size_t size)
{
    return std::string(data, data + (size > 1024 ? 1024 : size));
}

bool existsJsonParam(const nlohmann::json& params, const std::string& name)
{
    return params.find(name) != params.end();
}

void checkJsonParam(const nlohmann::json& params, const std::string& name, const JsonRpcId& id)
{
    if (!existsJsonParam(params, name))
        throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "Parameter '" + name + "' doesn't exist.", id };
}

void FillAddressData(const JsonRpcId& id, const nlohmann::json& params, AddressData& data)
{
    if (existsJsonParam(params, "comment"))
    {
        std::string comment = params["comment"];

        data.comment = comment;
    }

    if (existsJsonParam(params, "expiration"))
    {
        std::string expiration = params["expiration"];

        static std::map<std::string, AddressData::Expiration> Items =
        {
            {"expired", AddressData::Expired},
            {"24h",  AddressData::OneDay},
            {"never", AddressData::Never},
        };

        if(Items.count(expiration) == 0)
            throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "Unknown value for the 'expiration' parameter.", id };

        data.expiration = Items[expiration];
    }
}

CoinIDList readCoinsParameter(const JsonRpcId& id, const nlohmann::json& params)
{
    CoinIDList coins;

    if (!params["coins"].is_array() || params["coins"].size() <= 0)
        throw jsonrpc_exception{ ApiError::InvalidJsonRpc , "Coins parameter must be an array of strings (coin IDs).", id };

    for (const auto& cid : params["coins"])
    {
        if (!cid.is_string())
            throw jsonrpc_exception{ ApiError::InvalidJsonRpc , "Coin ID in the coins array must be a string.", id };

        std::string sCid = cid;
        auto coinId = Coin::FromString(sCid);
        if (!coinId)
        {
            const auto errmsg = std::string("Invalid 'coin ID' parameter: ") + std::string(sCid);
            throw jsonrpc_exception{ApiError::InvalidParamsJsonRpc, errmsg, id};
        }

        coins.push_back(*coinId);
    }

    return coins;
}

uint64_t readSessionParameter(const JsonRpcId& id, const nlohmann::json& params)
{
    uint64_t session = 0;

    if (params["session"].is_number_unsigned() && params["session"] > 0)
    {
        session = params["session"];
    }
    else throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "Invalid 'session' parameter.", id };

    return session;

}

void checkTxId(const ByteBuffer& txId, const JsonRpcId& id)
{
    if (txId.size() != TxID().size())
        throw jsonrpc_exception{ ApiError::InvalidTxId, "Transaction ID has wrong format.", id };
}

boost::optional<TxID> readTxIdParameter(const JsonRpcId& id, const nlohmann::json& params)
{
    boost::optional<TxID> txId;

    if (existsJsonParam(params, "txId"))
    {
        if (!params["txId"].is_string())
            throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "Transaction ID must be a hex string.", id };

        TxID txIdDst;
        auto txIdSrc = from_hex(params["txId"]);

        checkTxId(txIdSrc, id);

        std::copy_n(txIdSrc.begin(), TxID().size(), txIdDst.begin());
        txId = txIdDst;
    }

    return txId;
}

#ifdef BEAM_ATOMIC_SWAP_SUPPORT
void throwIncorrectCurrencyError(const std::string& name, const JsonRpcId& id)
{
    throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "wrong currency message here.", id };
}

std::string swapOfferStatusToString(const SwapOfferStatus& status)
{
    switch(status)
    {
    case SwapOfferStatus::Canceled : return "canceled";
    case SwapOfferStatus::Completed : return "completed";
    case SwapOfferStatus::Expired : return "expired";
    case SwapOfferStatus::Failed : return "failed";
    case SwapOfferStatus::InProgress : return "in progress";
    case SwapOfferStatus::Pending : return "pending";
    default : return "unknown";
    }
}

json OfferToJson(const SwapOffer& offer,
                 const std::vector<WalletAddress>& myAddresses,
                 const Height& systemHeight)
{
    const auto& publisherId = offer.m_publisherId;
    bool isOwnOffer = true;
    if (publisherId.cmp(Zero))
    {
        const auto& it = std::find_if(
            myAddresses.begin(), myAddresses.end(),
            [&publisherId] (const WalletAddress& wa) {
                return wa.m_walletID == publisherId;
            });
        isOwnOffer = it != myAddresses.end();
    }

    bool isSendBeamOffer =
        isOwnOffer ? !offer.isBeamSide() : offer.isBeamSide();

    Amount send =
        isSendBeamOffer ? offer.amountBeam() : offer.amountSwapCoin();
    Amount receive =
        isSendBeamOffer ? offer.amountSwapCoin() : offer.amountBeam();
    
    std::string sendCurrency =
        isSendBeamOffer ? "BEAM" : std::to_string(offer.swapCoinType());
    std::string sreceiveCurrency =
        isSendBeamOffer ? std::to_string(offer.swapCoinType()) : "BEAM";

    auto peerResponseTime = offer.peerResponseHeight();
    auto minHeight = offer.minHeight();

    Timestamp expiresTime = getTimestamp();
    if (systemHeight && peerResponseTime && minHeight)
    {
        auto expiresHeight = minHeight + peerResponseTime;
        auto currentDateTime = getTimestamp();

        expiresTime = systemHeight <= expiresHeight
            ? currentDateTime + (expiresHeight - systemHeight) * 60
            : currentDateTime - (systemHeight - expiresHeight) * 60;
    }

    auto createTimeStr = format_timestamp(kTimeStampFormat3x3,
                                        offer.timeCreated() * 1000,
                                        false);
    auto expiresTimeStr = format_timestamp(kTimeStampFormat3x3,
                                        expiresTime * 1000,
                                        false);
    
    json result {
        {"status", offer.m_status},
        {"status_string", swapOfferStatusToString(offer.m_status)},
        {"tx_id", TxIDToString(offer.m_txId)},
        {"send_amount", send},
        {"send_currency", sendCurrency},
        {"receive_amount", receive},
        {"receive_currency", sreceiveCurrency},
        {"is_my_offer", isOwnOffer},
        {"time_created", createTimeStr},
        {"time_expired", expiresTimeStr},
    };

    if (offer.m_status == SwapOfferStatus::Pending)
    {
        if (isOwnOffer)
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
#endif  // BEAM_ATOMIC_SWAP_SUPPORT

}  // namespace

    WalletApi::WalletApi(IWalletApiHandler& handler, ACL acl)
        : _handler(handler)
        , _acl(acl)
    {
#define REG_FUNC(api, name, writeAccess) \
        _methods[name] = {BIND_THIS_MEMFN(on##api##Message), writeAccess};

        WALLET_API_METHODS(REG_FUNC)

#undef REG_FUNC
    };

    void WalletApi::onCreateAddressMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        CreateAddress createAddress;
        FillAddressData(id, params, createAddress);

        _handler.onMessage(id, createAddress);
    }

    void WalletApi::onDeleteAddressMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        checkJsonParam(params, "address", id);

        DeleteAddress deleteAddress;
        deleteAddress.address.FromHex(params["address"]);

        _handler.onMessage(id, deleteAddress);
    }

    void WalletApi::onEditAddressMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        checkJsonParam(params, "address", id);

        if (!existsJsonParam(params, "comment") && !existsJsonParam(params, "expiration"))
            throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "Comment or Expiration parameter must be specified.", id };

        EditAddress editAddress;
        editAddress.address.FromHex(params["address"]);

        FillAddressData(id, params, editAddress);


        _handler.onMessage(id, editAddress);
    }

    void WalletApi::onAddrListMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        checkJsonParam(params, "own", id);

        AddrList addrList;

        addrList.own = params["own"];

        _handler.onMessage(id, addrList);
    }

    void WalletApi::onValidateAddressMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        checkJsonParam(params, "address", id);

        if (params["address"].empty())
            throw jsonrpc_exception{ ApiError::InvalidAddress, "Address is empty.", id };

        ValidateAddress validateAddress;
        validateAddress.address.FromHex(params["address"]);

        _handler.onMessage(id, validateAddress);
    }

    void WalletApi::onSendMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        checkJsonParam(params, "value", id);
        checkJsonParam(params, "address", id);

        if (!params["value"].is_number_unsigned() || params["value"] == 0)
            throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "Value must be non zero 64bit unsigned integer.", id };

        if (params["address"].empty())
            throw jsonrpc_exception{ ApiError::InvalidAddress, "Address is empty.", id };

        Send send;
        send.value = params["value"];

        if (existsJsonParam(params, "coins"))
        {
            send.coins = readCoinsParameter(id, params);
        }
        else if (existsJsonParam(params, "session"))
        {
            send.session = readSessionParameter(id, params);
        }

        if (!send.address.FromHex(params["address"]))
        {
            throw jsonrpc_exception{ ApiError::InvalidAddress , "Invalid receiver address.", id };
        }

        if (existsJsonParam(params, "from"))
        {
            WalletID from(Zero);
            if (from.FromHex(params["from"]))
            {
                send.from = from;
            }
            else
            {
                throw jsonrpc_exception{ ApiError::InvalidAddress, "Invalid sender address.", id };
            }
        }

        if (existsJsonParam(params, "fee"))
        {
            if(!params["fee"].is_number_unsigned() || params["fee"] == 0)
                throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "Invalid fee.", id };

            send.fee = params["fee"];
        }

        if (existsJsonParam(params, "comment"))
        {
            send.comment = params["comment"];
        }

        send.txId = readTxIdParameter(id, params);

        _handler.onMessage(id, send);
    }

    void WalletApi::onStatusMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        checkJsonParam(params, "txId", id);

        Status status;

        auto txId = from_hex(params["txId"]);

        checkTxId(txId, id);

        std::copy_n(txId.begin(), status.txId.size(), status.txId.begin());

        _handler.onMessage(id, status);
    }

    void WalletApi::onSplitMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        checkJsonParam(params, "coins", id);

        if (!params["coins"].is_array() || params["coins"].size() <= 0)
            throw jsonrpc_exception{ ApiError::InvalidParamsJsonRpc, "Coins parameter must be a nonempty array.", id };

        Split split;

        for (const auto& amount : params["coins"])
        {
            if(!amount.is_number_unsigned() || amount == 0)
                throw jsonrpc_exception{ ApiError::InvalidParamsJsonRpc, "Coin amount must be non zero 64bit unsigned integer.", id };

            split.coins.push_back(amount);
        }

        if (existsJsonParam(params, "fee"))
        {
            if (!params["fee"].is_number_unsigned() || params["fee"] == 0)
                throw jsonrpc_exception{ ApiError::InvalidParamsJsonRpc, "Invalid fee.", id };

            split.fee = params["fee"];
        }
        else
        {
            split.fee = std::max(wallet::GetMinimumFee(split.coins.size() + 1), DefaultFee); // +1 extra output for change
        }

        split.txId = readTxIdParameter(id, params);

        _handler.onMessage(id, split);
    }

    void WalletApi::onTxCancelMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        checkJsonParam(params, "txId", id);
        auto txId = from_hex(params["txId"]);

        TxCancel txCancel;

        checkTxId(txId, id);

        std::copy_n(txId.begin(), txCancel.txId.size(), txCancel.txId.begin());

        _handler.onMessage(id, txCancel);
    }

    void WalletApi::onTxDeleteMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        checkJsonParam(params, "txId", id);
        auto txId = from_hex(params["txId"]);

        TxDelete txDelete;

        checkTxId(txId, id);

        std::copy_n(txId.begin(), txDelete.txId.size(), txDelete.txId.begin());

        _handler.onMessage(id, txDelete);
    }

    void WalletApi::onIssueMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        checkJsonParam(params, "value", id);
        if (!Rules::get().CA.Enabled)
            throw jsonrpc_exception{ ApiError::NotSupported, "Confidential assets are not supported in this version.", id };

        if (!params["value"].is_number_unsigned() || params["value"] == 0)
            throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "Value must be non zero 64bit unsigned integer.", id };

        Issue issue;
        issue.value = params["value"];

        auto ind = params["index"];
        if (!params["index"].is_number_unsigned() || params["index"] == 0)
            throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "Index must be non zero 64bit unsigned integer.", id };

        issue.index = Key::Index (params["index"]);

        if (existsJsonParam(params, "coins"))
        {
            issue.coins = readCoinsParameter(id, params);
        }
        else if (existsJsonParam(params, "session"))
        {
            issue.session = readSessionParameter(id, params);
        }

        if (existsJsonParam(params, "fee"))
        {
            if(!params["fee"].is_number_unsigned() || params["fee"] == 0)
                throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "Fee must be non zero 64bit unsigned integer.", id };

            issue.fee = params["fee"];
        }

        issue.txId = readTxIdParameter(id, params);
        _handler.onMessage(id, issue);
    }

    void WalletApi::onGetUtxoMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        GetUtxo getUtxo;

        if (existsJsonParam(params, "count"))
        {
            if (params["count"] > 0)
            {
                getUtxo.count = params["count"];
            }
            else throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "Invalid 'count' parameter.", id };
        }

        if (existsJsonParam(params, "skip"))
        {
            if (params["skip"] >= 0)
            {
                getUtxo.skip = params["skip"];
            }
            else throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "Invalid 'skip' parameter.", id };
        }

        _handler.onMessage(id, getUtxo);
    }

    void WalletApi::onLockMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        checkJsonParam(params, "coins", id);
        checkJsonParam(params, "session", id);

        Lock lock;

        lock.session = readSessionParameter(id, params);
        lock.coins = readCoinsParameter(id, params);

        _handler.onMessage(id, lock);
    }

    void WalletApi::onUnlockMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        checkJsonParam(params, "session", id);

        Unlock unlock;

        unlock.session = readSessionParameter(id, params);

        _handler.onMessage(id, unlock);
    }

    void WalletApi::onTxListMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        TxList txList;

        if (existsJsonParam(params, "filter"))
        {
            if (existsJsonParam(params["filter"], "status")
                && params["filter"]["status"].is_number_unsigned())
            {
                txList.filter.status = (TxStatus)params["filter"]["status"];
            }

            if (existsJsonParam(params["filter"], "height")
                && params["filter"]["height"].is_number_unsigned())
            {
                txList.filter.height = (Height)params["filter"]["height"];
            }
        }

        if (existsJsonParam(params, "count"))
        {
            if (params["count"] > 0)
            {
                txList.count = params["count"];
            }
            else throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "Invalid 'count' parameter.", id };
        }

        if (existsJsonParam(params, "skip"))
        {
            if (params["skip"] >= 0)
            {
                txList.skip = params["skip"];
            }
            else throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "Invalid 'skip' parameter.", id };
        }

        _handler.onMessage(id, txList);
    }

    void WalletApi::onWalletStatusMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        WalletStatus walletStatus;
        _handler.onMessage(id, walletStatus);
    }

    void WalletApi::onGenerateTxIdMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        GenerateTxId generateTxId;
        _handler.onMessage(id, generateTxId);
    }

#if defined(BEAM_ATOMIC_SWAP_SUPPORT)

    void WalletApi::onOffersListMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        OffersList offersList;
        _handler.onMessage(id, offersList);
    }

    void WalletApi::onCreateOfferMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        CreateOffer data;

        checkJsonParam(params, "send_amount", id);
        if (!params["send_amount"].is_number_unsigned() ||
            params["send_amount"] == 0)
            throw jsonrpc_exception
            {
                ApiError::InvalidJsonRpc,
                "\'send_amount\' must be non zero 64bit unsigned integer.",
                id
            };
        Amount sendAmount = params["send_amount"];
        
        AtomicSwapCoin sendCoin = AtomicSwapCoin::Unknown;
        checkJsonParam(params, "send_currency", id);
        if (params["send_currency"].is_string())
        {
            sendCoin = from_string(params["send_currency"]);
        }
        else
        {
            throwIncorrectCurrencyError("send_currency", id);
        }
        if (sendCoin == AtomicSwapCoin::Unknown &&
            params["send_currency"] != "beam")
        {
            throwIncorrectCurrencyError("send_currency", id);
        }

        checkJsonParam(params, "receive_amount", id);
        if (!params["receive_amount"].is_number_unsigned() ||
            params["receive_amount"] == 0)
            throw jsonrpc_exception
            {
                ApiError::InvalidJsonRpc,
                "\'receive_amount\' must be non zero 64bit unsigned integer.",
                id
            };
        Amount receiveAmount = params["receive_amount"];
        
        AtomicSwapCoin receiveCoin = AtomicSwapCoin::Unknown;
        checkJsonParam(params, "receive_currency", id);
        if (params["receive_currency"].is_string())
        {
            receiveCoin = from_string(params["receive_currency"]);
        }
        else
        {
            throwIncorrectCurrencyError("receive_currency", id);
        }
        if (receiveCoin == AtomicSwapCoin::Unknown &&
            params["receive_currency"] != "beam")
        {
            throwIncorrectCurrencyError("receive_currency", id);
        }

        if (params["receive_currency"] != "beam" &&
            params["send_currency"] != "beam")
        {
            throw jsonrpc_exception
            {
                ApiError::InvalidJsonRpc,
                "\'receive_currency\' or \'send_currency\' must be 'beam'.",
                id
            };
        }

        if (sendCoin == receiveCoin)
        {
            throw jsonrpc_exception
            {
                ApiError::InvalidJsonRpc,
                "\'receive_currency\' and \'send_currency\' must be not same.",
                id
            };
        }

        data.isBeamSide = sendCoin == AtomicSwapCoin::Unknown;
        data.swapCoin = data.isBeamSide ? receiveCoin : sendCoin;
        data.beamAmount = data.isBeamSide ? sendAmount : receiveAmount;
        data.swapAmount = data.isBeamSide ? receiveAmount : sendAmount;

        checkJsonParam(params, "beam_fee", id);
        if (!params["beam_fee"].is_number_unsigned() ||
            params["beam_fee"] == 0)
            throw jsonrpc_exception
            {
                ApiError::InvalidJsonRpc,
                "\'beam_fee\' must be non zero 64bit unsigned integer.",
                id
            };
        data.beamFee = params["beam_fee"];

        std::string swapCoinFeeParamName = data.isBeamSide
            ? params["receive_currency"]
            : params["send_currency"];
        swapCoinFeeParamName += "_fee_rate";

        checkJsonParam(params, swapCoinFeeParamName, id);
        if (!params[swapCoinFeeParamName].is_number_unsigned() ||
            params[swapCoinFeeParamName] == 0)
        {
            auto message =
                swapCoinFeeParamName +
                " must be non zero 64bit unsigned integer.";
            throw jsonrpc_exception
            {
                ApiError::InvalidJsonRpc,
                message,
                id
            };
        }

        Amount swapFeeRate = params[swapCoinFeeParamName];
        switch(data.swapCoin)
        {
            case AtomicSwapCoin::Bitcoin:
                data.swapFee = BitcoinSide::CalcTotalFee(swapFeeRate);
                break;
            case AtomicSwapCoin::Litecoin:
                data.swapFee = LitecoinSide::CalcTotalFee(swapFeeRate);
                break;
            case AtomicSwapCoin::Qtum:
                data.swapFee = QtumSide::CalcTotalFee(swapFeeRate);
                break;
            default:
                throw jsonrpc_exception
                {
                    ApiError::InvalidJsonRpc,
                    "swap coin undefined",
                    id
                };
        }

        checkJsonParam(params, "offer_expires", id);
        if (!params["offer_expires"].is_number_unsigned() ||
            params["offer_expires"] == 0)
            throw jsonrpc_exception
            {
                ApiError::InvalidJsonRpc,
                "\'offer_expires\' must be non zero 64bit unsigned integer.",
                id
            };
        data.offerLifetime = params["offer_expires"];

        if (existsJsonParam(params, "comment") &&
            params["comment"].is_string())
        {
            data.comment = params["comment"];
        }
        
        try
        {
            _handler.onMessage(id, data);
        }
        catch(const FailToStartNewTransactionException&)
        {
            throw jsonrpc_exception
            {
                ApiError::InvalidJsonRpc,
                "Can't create transaction.",
                id
            };
        }
    }

    void WalletApi::onPublishOfferMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        PublishOffer data;
        _handler.onMessage(id, data);
    }

    void WalletApi::onAcceptOfferMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        AcceptOffer data;
        _handler.onMessage(id, data);
    }

    void WalletApi::onCancelOfferMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        CancelOffer data;
        _handler.onMessage(id, data);
    }

    void WalletApi::onOfferStatusMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        checkJsonParam(params, "token", id);
        const auto& token = params["token"];

        if (!SwapOfferToken::isValid(token))
            throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "Parameter 'token' not valid.", id };

        
        auto parameters = beam::wallet::ParseParameters(token);
        if (!parameters)
            throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "Parse Parameters from 'token' failed.", id };

        SwapOffer offer;
        offer.SetTxParameters(parameters->Pack());
           

        OfferStatus data{token, offer};
        _handler.onMessage(id, data);
    }
#endif

    void WalletApi::getResponse(const JsonRpcId& id, const CreateAddress::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result", std::to_string(res.address)}
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const DeleteAddress::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result", "done"}
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const EditAddress::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result", "done"}
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const AddrList::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
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
                {"own", addr.isOwn()}
            });
        }
    }

    void WalletApi::getResponse(const JsonRpcId& id, const ValidateAddress::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
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
            {JsonRpcHrd, JsonRpcVerHrd},
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
                {"amount", utxo.m_ID.m_Value},
                {"type", (const char*)FourCC::Text(utxo.m_ID.m_Type)},
                {"maturity", utxo.get_Maturity()},
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
            {JsonRpcHrd, JsonRpcVerHrd},
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
            {JsonRpcHrd, JsonRpcVerHrd},
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
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result", {}}
        };

        GetStatusResponseJson(
            res.tx, msg["result"], res.kernelProofHeight, res.systemHeight);
    }

    void WalletApi::getResponse(const JsonRpcId& id, const Split::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result",
                {
                    {"txId", to_hex(res.txId.data(), res.txId.size())}
                }
            }
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const TxCancel::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result", res.result}
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const TxDelete::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result", res.result}
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const TxList::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result", json::array()}
        };

        for (const auto& resItem : res.resultList)
        {
            json item = {};
            GetStatusResponseJson(
                resItem.tx,
                item,
                resItem.kernelProofHeight,
                resItem.systemHeight);
            msg["result"].push_back(item);
        }
    }

    void WalletApi::getResponse(const JsonRpcId& id, const WalletStatus::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result",
                {
                    {"current_height", res.currentHeight},
                    {"current_state_hash", to_hex(res.currentStateHash.m_pData, res.currentStateHash.nBytes)},
                    {"prev_state_hash", to_hex(res.prevStateHash.m_pData, res.prevStateHash.nBytes)},
                    {"available", res.available},
                    {"receiving", res.receiving},
                    {"sending", res.sending},
                    {"maturing", res.maturing},
                    {"difficulty", res.difficulty},
                }
            }
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const GenerateTxId::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result", TxIDToString(res.txId)}
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const Lock::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result", res.result}
        };        
    }

    void WalletApi::getResponse(const JsonRpcId& id, const Unlock::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result", res.result}
        };
    }

    bool WalletApi::parse(const char* data, size_t size)
    {
        if (size == 0)
        {
            json msg
            {
                {JsonRpcHrd, JsonRpcVerHrd},
                {"error",
                    {
                        {"code", ApiError::InvalidJsonRpc},
                        {"message", "Empty JSON request."},
                    }
                }
            };

            _handler.onInvalidJsonRpc(msg);
            return false;
        }

        try
        {
            json msg = json::parse(data, data + size);

            if(!msg["id"].is_number_integer() 
                && !msg["id"].is_string())
                throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "ID can be integer or string only." };

            JsonRpcId id = msg["id"];

            if (msg[JsonRpcHrd] != JsonRpcVerHrd) 
                throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "Invalid JSON-RPC 2.0 header.", id };

            if (_acl)
            {
                if (msg["key"] == nullptr) 
                    throw jsonrpc_exception{ ApiError::InvalidParamsJsonRpc , "API key not specified.", id };

                if (_acl->count(msg["key"]) == 0) 
                    throw jsonrpc_exception{ ApiError::UnknownApiKey , msg["key"], id };
            }

            checkJsonParam(msg, "method", id);

            JsonRpcId method = msg["method"];

            if (_methods.find(method) == _methods.end())
            {
                throw jsonrpc_exception{ ApiError::NotFoundJsonRpc, method, id };
            }

            try
            {
                auto& info = _methods[method];

                if(_acl && info.writeAccess && _acl.get()[msg["key"]] == false)
                {
                    throw jsonrpc_exception{ ApiError::InvalidParamsJsonRpc , "User doesn't have permissions to call this method.", id };
                }

                info.func(id, msg["params"] == nullptr ? json::object() : msg["params"]);
            }
            catch (const nlohmann::detail::exception& e)
            {
                LOG_ERROR() << "json parse: " << e.what() << "\n" << getJsonString(data, size);

                throw jsonrpc_exception{ ApiError::InvalidJsonRpc , e.what(), id };
            }
        }
        catch (const jsonrpc_exception& e)
        {
            json msg
            {
                {JsonRpcHrd, JsonRpcVerHrd},
                {"error",
                    {
                        {"code", e.code},
                        {"message", getErrorMessage(e.code)},
                    }
                }
            };

            if (!e.data.empty())
            {
                msg["error"]["data"] = e.data;
            }

            if (e.id.is_number_integer() || e.id.is_string()) msg["id"] = e.id;
            else msg.erase("id");

            _handler.onInvalidJsonRpc(msg);
        }
        catch (const std::exception& e)
        {
            json msg
            {
                {JsonRpcHrd, JsonRpcVerHrd},
                {"error",
                    {
                        {"code", ApiError::InternalErrorJsonRpc},
                        {"message", e.what()},
                    }
                }
            };

            _handler.onInvalidJsonRpc(msg);
        }

        return true;
    }

    const char* WalletApi::getErrorMessage(ApiError code)
    {
#define ERROR_ITEM(_, item, info) case item: return info;
        switch (code) { JSON_RPC_ERRORS(ERROR_ITEM) }
#undef ERROR_ITEM

        assert(false);

        return "unknown error.";
    }

#if defined(BEAM_ATOMIC_SWAP_SUPPORT)    
    void WalletApi::getResponse(const JsonRpcId& id, const OffersList::Response& res, json& msg)
    {
        msg = 
        {
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result", json::array()}
        };

        for (auto& offer : res.list)
        {
             msg["result"].push_back(
                OfferToJson(offer, res.addrList, res.systemHeight));
        }
    }

    void WalletApi::getResponse(const JsonRpcId& id, const CreateOffer::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result", 
            {
                {"token", res.token}
            }}
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const PublishOffer::Response& res, json& msg)
    {
        msg = getNotImplError(id);
    }

    void WalletApi::getResponse(const JsonRpcId& id, const AcceptOffer::Response& res, json& msg)
    {
        msg = getNotImplError(id);
    }

    void WalletApi::getResponse(const JsonRpcId& id, const CancelOffer::Response& res, json& msg)
    {
        msg = getNotImplError(id);
    }

    void WalletApi::getResponse(const JsonRpcId& id, const OfferStatus::Response& res, json& msg)
    {
        msg = 
        {
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result", OfferToJson(res.offer, res.addrList, res.systemHeight)}
        };
        // TODO(zavarza)
    }
#endif  // BEAM_ATOMIC_SWAP_SUPPORT

}  // namespace beam::wallet
