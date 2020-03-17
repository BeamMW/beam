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
#include "wallet/client/extensions/offers_board/swap_offer_token.h"
#include "wallet/transactions/swaps/bridges/bitcoin/bitcoin_side.h"
#include "wallet/transactions/swaps/bridges/litecoin/litecoin_side.h"
#include "wallet/transactions/swaps/bridges/qtum/qtum_side.h"
#include "wallet/transactions/swaps/utils.h"
#endif  // BEAM_ATOMIC_SWAP_SUPPORT

namespace beam::wallet
{    
namespace
{

json getNotImplError(const JsonRpcId& id)
{
    return json
    {
        {Api::JsonRpcHrd, Api::JsonRpcVerHrd},
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

CoinIDList readCoinsParameter(const JsonRpcId& id, const json& params)
{
    CoinIDList coins;

    if (!params["coins"].is_array() || params["coins"].size() <= 0)
        throw WalletApi::jsonrpc_exception{ ApiError::InvalidJsonRpc , "Coins parameter must be an array of strings (coin IDs).", id };

    for (const auto& cid : params["coins"])
    {
        if (!cid.is_string())
            throw WalletApi::jsonrpc_exception{ ApiError::InvalidJsonRpc , "Coin ID in the coins array must be a string.", id };

        std::string sCid = cid;
        auto coinId = Coin::FromString(sCid);
        if (!coinId)
        {
            const auto errmsg = std::string("Invalid 'coin ID' parameter: ") + std::string(sCid);
            throw WalletApi::jsonrpc_exception{ApiError::InvalidParamsJsonRpc, errmsg, id};
        }

        coins.push_back(*coinId);
    }

    return coins;
}

uint64_t readSessionParameter(const JsonRpcId& id, const json& params)
{
    uint64_t session = 0;

    if (params["session"].is_number_unsigned() && params["session"] > 0)
    {
        session = params["session"];
    }
    else throw WalletApi::jsonrpc_exception{ ApiError::InvalidJsonRpc, "Invalid 'session' parameter.", id };

    return session;

}

void checkTxId(const ByteBuffer& txId, const JsonRpcId& id)
{
    if (txId.size() != TxID().size())
        throw WalletApi::jsonrpc_exception{ ApiError::InvalidTxId, "Transaction ID has wrong format.", id };
}

boost::optional<TxID> readTxIdParameter(const JsonRpcId& id, const json& params)
{
    boost::optional<TxID> txId;

    if (WalletApi::existsJsonParam(params, "txId"))
    {
        if (!params["txId"].is_string())
            throw WalletApi::jsonrpc_exception{ ApiError::InvalidJsonRpc, "Transaction ID must be a hex string.", id };

        TxID txIdDst;
        auto txIdSrc = from_hex(params["txId"]);

        checkTxId(txIdSrc, id);

        std::copy_n(txIdSrc.begin(), TxID().size(), txIdDst.begin());
        txId = txIdDst;
    }

    return txId;
}

// return 0 if parameter not found
Amount readBeamFeeParameter(const JsonRpcId& id, const json& params,
    const std::string& paramName = "fee",
    Amount minimumFee = std::max(wallet::GetMinimumFee(2), kMinFeeInGroth)) // receivers's output + change
{
    if (!WalletApi::existsJsonParam(params, paramName))
    {
        return 0;
    }

    if (!params[paramName].is_number_unsigned() || params[paramName] == 0)
    {
        std::stringstream ss;
        ss << "\"" << paramName << "\" " << "must be non zero 64bit unsigned integer.";
        throw WalletApi::jsonrpc_exception{ ApiError::InvalidJsonRpc, ss.str(), id };
    }

    Amount fee = params[paramName];

    if (fee < minimumFee)
    {
        std::stringstream ss;
        ss << "Failed to initiate the operation. The minimum fee is " << minimumFee << " GROTH.";
        throw WalletApi::jsonrpc_exception{ ApiError::InternalErrorJsonRpc, ss.str(), id };
    }

    return fee;
}

Amount readSwapFeeRateParameter(const JsonRpcId& id, const json& params)
{
    if (!WalletApi::existsJsonParam(params, "fee_rate"))
    {
        return 0;
    }

    if (!params["fee_rate"].is_number_unsigned() || params["fee_rate"] == 0)
    {
        auto message = "\"fee_rate\" must be non zero 64bit unsigned integer.";
        throw WalletApi::jsonrpc_exception
        {
            ApiError::InvalidJsonRpc,
            message,
            id
        };
    }
    return params["fee_rate"];
}

static void FillAddressData(const JsonRpcId& id, const json& params, AddressData& data)
{
    if (WalletApi::existsJsonParam(params, "comment"))
    {
        std::string comment = params["comment"];

        data.comment = comment;
    }

    if (WalletApi::existsJsonParam(params, "expiration"))
    {
        std::string expiration = params["expiration"];

        static std::map<std::string, AddressData::Expiration> Items =
        {
            {"expired", AddressData::Expired},
            {"24h",  AddressData::OneDay},
            {"never", AddressData::Never},
        };

        if(Items.count(expiration) == 0)
            throw WalletApi::jsonrpc_exception{ ApiError::InvalidJsonRpc, "Unknown value for the 'expiration' parameter.", id };

        data.expiration = Items[expiration];
    }
}

#ifdef BEAM_ATOMIC_SWAP_SUPPORT
void throwIncorrectCurrencyError(const std::string& name, const JsonRpcId& id)
{
    throw WalletApi::jsonrpc_exception{ ApiError::InvalidJsonRpc, "wrong currency message here.", id };
}

std::string swapOfferStatusToString(const SwapOfferStatus& status)
{
    switch(status)
    {
    case SwapOfferStatus::Canceled : return "cancelled";
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
    bool isPublic = false;
    if (publisherId.cmp(Zero))
    {
        isPublic = true;
        isOwnOffer = storage::isMyAddress(myAddresses, publisherId);
    }
    else
    {
        auto peerId = offer.GetParameter<WalletID>(TxParameterID::PeerID);
        auto myId = offer.GetParameter<WalletID>(TxParameterID::MyID);
        if (peerId && !myId)
            isOwnOffer = false;
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

OfferInput collectOfferInput(const JsonRpcId& id, const json& params)
{
    OfferInput data;
    WalletApi::checkJsonParam(params, "send_amount", id);
    if (!params["send_amount"].is_number_unsigned() ||
        params["send_amount"] == 0)
        throw WalletApi::jsonrpc_exception
        {
            ApiError::InvalidJsonRpc,
            "\'send_amount\' must be non zero 64bit unsigned integer.",
            id
        };
    Amount sendAmount = params["send_amount"];
        
    AtomicSwapCoin sendCoin = AtomicSwapCoin::Unknown;
    WalletApi::checkJsonParam(params, "send_currency", id);
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

    WalletApi::checkJsonParam(params, "receive_amount", id);
    if (!params["receive_amount"].is_number_unsigned() ||
        params["receive_amount"] == 0)
        throw WalletApi::jsonrpc_exception
        {
            ApiError::InvalidJsonRpc,
            "\'receive_amount\' must be non zero 64bit unsigned integer.",
            id
        };
    Amount receiveAmount = params["receive_amount"];

    AtomicSwapCoin receiveCoin = AtomicSwapCoin::Unknown;
    WalletApi::checkJsonParam(params, "receive_currency", id);
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
        throw WalletApi::jsonrpc_exception
        {
            ApiError::InvalidJsonRpc,
            "\'receive_currency\' or \'send_currency\' must be 'beam'.",
            id
        };
    }

    if (sendCoin == receiveCoin)
    {
        throw WalletApi::jsonrpc_exception
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

    if (auto beamFee = readBeamFeeParameter(id, params, "beam_fee"); beamFee)
    {
        data.beamFee = beamFee;

        if (data.isBeamSide && data.beamAmount < data.beamFee)
        {
            throw WalletApi::jsonrpc_exception
            {
                ApiError::InvalidJsonRpc,
                "\'beam_fee\' must be non zero 64bit unsigned integer.",
                id
            };
        }
    }

    if (auto feeRate = readSwapFeeRateParameter(id, params); feeRate)
    {
        data.swapFeeRate = feeRate;
    }

    if (WalletApi::existsJsonParam(params, "offer_expires"))
    {
        if (!params["offer_expires"].is_number_unsigned() ||
            params["offer_expires"] == 0)
        {
            throw WalletApi::jsonrpc_exception
            {
                ApiError::InvalidJsonRpc,
                "\'offer_expires\' must be non zero 64bit unsigned integer.",
                id
            };
        }
        data.offerLifetime = params["offer_expires"];
    }

    if (WalletApi::existsJsonParam(params, "comment") &&
        params["comment"].is_string())
    {
        data.comment = params["comment"];
    }

    return data;
}
#endif  // BEAM_ATOMIC_SWAP_SUPPORT

}  // namespace

    Api::Api(IApiHandler& handler, ACL acl)
        : _handler(handler)
        , _acl(acl)
    {

    }

    // static
    bool Api::existsJsonParam(const json& params, const std::string& name)
    {
        return params.find(name) != params.end();
    }

    // static
    void Api::checkJsonParam(const json& params, const std::string& name, const JsonRpcId& id)
    {
        if (!existsJsonParam(params, name))
            throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "Parameter '" + name + "' doesn't exist.", id };
    }

    bool Api::parse(const char* data, size_t size)
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

    // static
    const char* Api::getErrorMessage(ApiError code)
    {
#define ERROR_ITEM(_, item, info) case item: return info;
        switch (code) { JSON_RPC_ERRORS(ERROR_ITEM) }
#undef ERROR_ITEM

        assert(false);

        return "unknown error.";
    }

    WalletApi::WalletApi(IWalletApiHandler& handler, ACL acl)
        : Api(handler, acl)
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
    };

    IWalletApiHandler& WalletApi::getHandler() const
    {
        return static_cast<IWalletApiHandler&>(_handler);
    }

    void WalletApi::onCreateAddressMessage(const JsonRpcId& id, const json& params)
    {
        CreateAddress createAddress;
        FillAddressData(id, params, createAddress);

        getHandler().onMessage(id, createAddress);
    }

    void WalletApi::onDeleteAddressMessage(const JsonRpcId& id, const json& params)
    {
        checkJsonParam(params, "address", id);

        DeleteAddress deleteAddress;
        deleteAddress.address.FromHex(params["address"]);

        getHandler().onMessage(id, deleteAddress);
    }

    void WalletApi::onEditAddressMessage(const JsonRpcId& id, const json& params)
    {
        checkJsonParam(params, "address", id);

        if (!existsJsonParam(params, "comment") && !existsJsonParam(params, "expiration"))
            throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "Comment or Expiration parameter must be specified.", id };

        EditAddress editAddress;
        editAddress.address.FromHex(params["address"]);

        FillAddressData(id, params, editAddress);


        getHandler().onMessage(id, editAddress);
    }

    void WalletApi::onAddrListMessage(const JsonRpcId& id, const json& params)
    {
        checkJsonParam(params, "own", id);

        AddrList addrList;

        addrList.own = params["own"];

        getHandler().onMessage(id, addrList);
    }

    void WalletApi::onValidateAddressMessage(const JsonRpcId& id, const json& params)
    {
        checkJsonParam(params, "address", id);

        if (params["address"].empty())
            throw jsonrpc_exception{ ApiError::InvalidAddress, "Address is empty.", id };

        ValidateAddress validateAddress;
        validateAddress.address.FromHex(params["address"]);

        getHandler().onMessage(id, validateAddress);
    }

    void WalletApi::onSendMessage(const JsonRpcId& id, const json& params)
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

        auto txParams = ParseParameters(params["address"]);
        if (!txParams)
        {
            throw jsonrpc_exception{ ApiError::InvalidAddress , "Invalid receiver address or token.", id };
        }
        send.txParameters = *txParams;

        if (auto peerID = send.txParameters.GetParameter<WalletID>(TxParameterID::PeerID); peerID)
        {
            send.address = *peerID;
        }
        else
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

        if (auto beamFee = readBeamFeeParameter(id, params); beamFee)
        {
            send.fee = beamFee;
        }

        if (existsJsonParam(params, "comment"))
        {
            send.comment = params["comment"];
        }

        send.txId = readTxIdParameter(id, params);

        getHandler().onMessage(id, send);
    }

    void WalletApi::onStatusMessage(const JsonRpcId& id, const json& params)
    {
        checkJsonParam(params, "txId", id);

        Status status;

        auto txId = from_hex(params["txId"]);

        checkTxId(txId, id);

        std::copy_n(txId.begin(), status.txId.size(), status.txId.begin());

        getHandler().onMessage(id, status);
    }

    void WalletApi::onSplitMessage(const JsonRpcId& id, const json& params)
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

        auto minimumFee = std::max(wallet::GetMinimumFee(split.coins.size() + 1), kMinFeeInGroth); // +1 extra output for change
        if (auto beamFee = readBeamFeeParameter(id, params, "fee", minimumFee); beamFee)
        {
            split.fee = beamFee;
        }
        else
        {
            split.fee = minimumFee;
        }

        split.txId = readTxIdParameter(id, params);

        getHandler().onMessage(id, split);
    }

    void WalletApi::onTxCancelMessage(const JsonRpcId& id, const json& params)
    {
        checkJsonParam(params, "txId", id);
        auto txId = from_hex(params["txId"]);

        TxCancel txCancel;

        checkTxId(txId, id);

        std::copy_n(txId.begin(), txCancel.txId.size(), txCancel.txId.begin());

        getHandler().onMessage(id, txCancel);
    }

    void WalletApi::onTxDeleteMessage(const JsonRpcId& id, const json& params)
    {
        checkJsonParam(params, "txId", id);
        auto txId = from_hex(params["txId"]);

        TxDelete txDelete;

        checkTxId(txId, id);

        std::copy_n(txId.begin(), txDelete.txId.size(), txDelete.txId.begin());

        getHandler().onMessage(id, txDelete);
    }

    void WalletApi::onIssueMessage(const JsonRpcId& id, const json& params)
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

        if (auto beamFee = readBeamFeeParameter(id, params); beamFee)
        {
            issue.fee = beamFee;
        }

        issue.txId = readTxIdParameter(id, params);
        getHandler().onMessage(id, issue);
    }

    void WalletApi::onGetUtxoMessage(const JsonRpcId& id, const json& params)
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

        getHandler().onMessage(id, getUtxo);
    }

    void WalletApi::onLockMessage(const JsonRpcId& id, const json& params)
    {
        checkJsonParam(params, "coins", id);
        checkJsonParam(params, "session", id);

        Lock lock;

        lock.session = readSessionParameter(id, params);
        lock.coins = readCoinsParameter(id, params);

        getHandler().onMessage(id, lock);
    }

    void WalletApi::onUnlockMessage(const JsonRpcId& id, const json& params)
    {
        checkJsonParam(params, "session", id);

        Unlock unlock;

        unlock.session = readSessionParameter(id, params);

        getHandler().onMessage(id, unlock);
    }

    void WalletApi::onTxListMessage(const JsonRpcId& id, const json& params)
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

        getHandler().onMessage(id, txList);
    }

    void WalletApi::onWalletStatusMessage(const JsonRpcId& id, const json& params)
    {
        WalletStatus walletStatus;
        getHandler().onMessage(id, walletStatus);
    }

    void WalletApi::onGenerateTxIdMessage(const JsonRpcId& id, const json& params)
    {
        GenerateTxId generateTxId;
        getHandler().onMessage(id, generateTxId);
    }

    void WalletApi::onExportPaymentProofMessage(const JsonRpcId& id, const json& params)
    {
        checkJsonParam(params, "txId", id);
        auto txId = from_hex(params["txId"]);
        checkTxId(txId, id);

        ExportPaymentProof data;
        std::copy_n(txId.begin(), data.txId.size(), data.txId.begin());

        getHandler().onMessage(id, data);
    }

    void WalletApi::onVerifyPaymentProofMessage(const JsonRpcId& id, const json& params)
    {
        checkJsonParam(params, "payment_proof", id);

        VerifyPaymentProof data;
        data.paymentProof = from_hex(params["payment_proof"]);

        getHandler().onMessage(id, data);
    }

#ifdef BEAM_ATOMIC_SWAP_SUPPORT

    void WalletApi::onOffersListMessage(const JsonRpcId& id, const json& params)
    {
        OffersList offersList;

        if (existsJsonParam(params, "filter"))
        {
            if (existsJsonParam(params["filter"], "swapCoin"))
            {
                if (params["filter"]["swapCoin"].is_string())
                {
                    offersList.filter.swapCoin = from_string(params["filter"]["swapCoin"]);
                }
            }

            if (existsJsonParam(params["filter"], "status"))
            {
                if (params["filter"]["status"].is_number_unsigned())
                {
                    offersList.filter.status = static_cast<SwapOfferStatus>(params["filter"]["status"]);
                }
            }
        }

        getHandler().onMessage(id, offersList);
    }

    void WalletApi::onOffersBoardMessage(const JsonRpcId& id, const json& params)
    {
        OffersBoard offersBoard;

        if (existsJsonParam(params, "filter"))
        {
            if (existsJsonParam(params["filter"], "swapCoin"))
            {
                if (params["filter"]["swapCoin"].is_string())
                {
                    offersBoard.filter.swapCoin = from_string(params["filter"]["swapCoin"]);
                }
            }
        }

        getHandler().onMessage(id, offersBoard);
    }

    void WalletApi::onCreateOfferMessage(const JsonRpcId& id, const json& params)
    {
        CreateOffer data = collectOfferInput(id, params);
        getHandler().onMessage(id, data);
    }

    void WalletApi::onPublishOfferMessage(const JsonRpcId& id, const json& params)
    {
        checkJsonParam(params, "token", id);
        const auto& token = params["token"];

        if (!SwapOfferToken::isValid(token))
            throw jsonrpc_exception
            {
                ApiError::InvalidParamsJsonRpc,
                "Parameter 'token' is not valid swap token.",
                id
            };

        PublishOffer data{token};
        getHandler().onMessage(id, data);
    }

    void WalletApi::onAcceptOfferMessage(const JsonRpcId& id, const json& params)
    {
        checkJsonParam(params, "token", id);
        const auto& token = params["token"];

        if (!SwapOfferToken::isValid(token))
            throw jsonrpc_exception
            {
                ApiError::InvalidParamsJsonRpc,
                "Parameter 'token' is not valid swap token.",
                id
            };

        AcceptOffer data;
        data.token = token;

        if (auto beamFee = readBeamFeeParameter(id, params, "beam_fee"); beamFee)
        {
            data.beamFee = beamFee;
        }

        if (auto feeRate = readSwapFeeRateParameter(id, params); feeRate)
        {
            data.swapFeeRate = feeRate;
        }

        if (WalletApi::existsJsonParam(params, "comment") && params["comment"].is_string())
        {
            data.comment = params["comment"];
        }

        getHandler().onMessage(id, data);
    }

    void WalletApi::onOfferStatusMessage(const JsonRpcId& id, const json& params)
    {
        checkJsonParam(params, "tx_id", id);
        auto txId = from_hex(params["tx_id"]);

        OfferStatus offerStatus;

        checkTxId(txId, id);

        std::copy_n(txId.begin(), offerStatus.txId.size(), offerStatus.txId.begin());
        
        getHandler().onMessage(id, offerStatus);
    }

    void WalletApi::onDecodeTokenMessage(const JsonRpcId& id, const json& params)
    {
        checkJsonParam(params, "token", id);
        const auto& token = params["token"];

        if (!SwapOfferToken::isValid(token))
        {
            throw jsonrpc_exception
            {
                ApiError::InvalidParamsJsonRpc,
                "Parameter 'token' is not valid swap token.",
                id
            };
        }

        DecodeToken decodeToken{ token };
        getHandler().onMessage(id, decodeToken);
    }

    void WalletApi::onGetBalanceMessage(const JsonRpcId& id, const json& params)
    {
        checkJsonParam(params, "coin", id);
        AtomicSwapCoin coin = AtomicSwapCoin::Unknown;
        if (params["coin"].is_string())
        {
            coin = from_string(params["coin"]);
        }

        if (coin == AtomicSwapCoin::Unknown)
        {
            throw jsonrpc_exception
            {
                ApiError::InvalidParamsJsonRpc,
                "Unknown coin.",
                id
            };
        }

        GetBalance data{ coin };
        getHandler().onMessage(id, data);
    }
#endif  // BEAM_ATOMIC_SWAP_SUPPORT

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
                {"own", addr.isOwn()},
                {"ownIDBase64", to_base64(addr.m_OwnID)}
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
                    {"txId", TxIDToString(res.txId)}
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

    void WalletApi::getResponse(const JsonRpcId& id, const ExportPaymentProof::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
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
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result", 
                {
                    {"is_valid", res.paymentInfo.IsValid()},

                    {"sender", std::to_string(res.paymentInfo.m_Sender)},
                    {"receiver", std::to_string(res.paymentInfo.m_Receiver)},
                    {"amount", res.paymentInfo.m_Amount},
                    {"kernel", std::to_string(res.paymentInfo.m_KernelID)},
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

    void WalletApi::getResponse(const JsonRpcId& id, const OffersBoard::Response& res, json& msg)
    {
        msg =
        {
            {JsonRpcHrd, JsonRpcVerHrd},
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
            {JsonRpcHrd, JsonRpcVerHrd},
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
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result", OfferToJson(res.offer, res.addrList, res.systemHeight)}
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const AcceptOffer::Response& res, json& msg)
    {
        msg = 
        {
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result", OfferToJson(res.offer, res.addrList, res.systemHeight)}
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const OfferStatus::Response& res, json& msg)
    {
        msg = 
        {
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result", OfferStatusToJson(res.offer, res.systemHeight)}
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const DecodeToken::Response& res, json& msg)
    {
        msg =
        {
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result", TokenToJson(res.offer, res.isMyOffer, res.isPublic)}
        };
    }

    void WalletApi::getResponse(const JsonRpcId& id, const GetBalance::Response& res, json& msg)
    {
        msg =
        {
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result",
            {
                {"available", res.available},
            }}
        };
    }
#endif  // BEAM_ATOMIC_SWAP_SUPPORT

}  // namespace beam::wallet
