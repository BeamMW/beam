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
#include "wallet/client/extensions/offers_board/swap_offer_token.h"
#include "wallet/transactions/swaps/common.h"
#include "wallet/transactions/swaps/swap_transaction.h"
#include "wallet/core/common_utils.h"
#include "wallet_api.h"

namespace beam::wallet
{
    namespace
    {
        using beam::wallet::json;

        json OfferStatusToJson(const SwapOffer& offer, const Height& systemHeight)
        {
            auto expiredHeight = offer.minHeight() + offer.peerResponseHeight();
            auto createTimeStr = format_timestamp(kTimeStampFormat3x3, offer.timeCreated() * 1000, false);
            json result{
                {"status", offer.m_status},
                {"status_string", swapOfferStatusToString(offer.m_status)},
                {"tx_id", std::to_string(offer.m_txId)},
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
                {"tx_id", std::to_string(offer.m_txId)},
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
                {"txId", std::to_string(offer.m_txId)},
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

        void throwIncorrectCurrencyError(const std::string& name, const JsonRpcId& id)
        {
            const std::string message = std::string("wrong ") + name + std::string(" currency.");
            throw jsonrpc_exception(ApiError::InvalidJsonRpc, message);
        }

        Amount readSwapFeeRateParameter(const JsonRpcId& id, const json& params)
        {
            return WalletApi::getMandatoryParam<PositiveAmount>(params, "fee_rate");
        }
    }

    std::pair<OffersList, IWalletApi::MethodInfo> WalletApi::onParseOffersList(const JsonRpcId& id, const json& params)
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

        return std::make_pair(offersList, MethodInfo());
    }

    std::pair<OffersBoard, IWalletApi::MethodInfo> WalletApi::onParseOffersBoard(const JsonRpcId& id, const json& params)
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

        return std::make_pair(offersBoard, MethodInfo());
    }

    std::pair<CreateOffer, IWalletApi::MethodInfo> WalletApi::onParseCreateOffer(const JsonRpcId& id, const json& params)
    {
        CreateOffer data;

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

        return std::make_pair(data, MethodInfo());
    }

    std::pair<PublishOffer, IWalletApi::MethodInfo> WalletApi::onParsePublishOffer(const JsonRpcId& id, const json& params)
    {
        const auto token = getMandatoryParam<NonEmptyString>(params, "token");
        if (!SwapOfferToken::isValid(token))
        {
            throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, "Parameter 'token' is not valid swap token.");
        }

        PublishOffer data{token};
        return std::make_pair(data, MethodInfo());
    }

    std::pair<AcceptOffer, IWalletApi::MethodInfo> WalletApi::onParseAcceptOffer(const JsonRpcId& id, const json& params)
    {
        AcceptOffer data;
        const auto token = getMandatoryParam<NonEmptyString>(params, "token");

        if (!SwapOfferToken::isValid(token))
        {
            throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, "Parameter 'token' is not valid swap token.");
        }

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

        return std::make_pair(data, MethodInfo());
    }

    std::pair<OfferStatus, IWalletApi::MethodInfo> WalletApi::onParseOfferStatus(const JsonRpcId& id, const json& params)
    {
        OfferStatus offerStatus = {};
        offerStatus.txId = getMandatoryParam<ValidTxID>(params, "tx_id");
        return std::make_pair(offerStatus, MethodInfo());
    }

    std::pair<DecodeToken, IWalletApi::MethodInfo> WalletApi::onParseDecodeToken(const JsonRpcId& id, const json& params)
    {
        const auto token = getMandatoryParam<NonEmptyString>(params, "token");

        if (!SwapOfferToken::isValid(token))
        {
            throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, "Parameter 'token' is not valid swap token.");
        }

        DecodeToken decodeToken{ token };
        return std::make_pair(decodeToken, MethodInfo());
    }

    std::pair<GetBalance, IWalletApi::MethodInfo> WalletApi::onParseGetBalance(const JsonRpcId& id, const json& params)
    {
        const auto coinName = getMandatoryParam<NonEmptyString>(params, "coin");
        const auto coin = from_string(coinName);

        if (coin == AtomicSwapCoin::Unknown)
        {
            throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, "Unknown coin.");
        }

        GetBalance data{ coin };
        return std::make_pair(data, MethodInfo());
    }

    std::pair<RecommendedFeeRate, IWalletApi::MethodInfo> WalletApi::onParseRecommendedFeeRate(const JsonRpcId& id, const json& params)
    {
        const auto coinName = getMandatoryParam<NonEmptyString>(params, "coin");
        AtomicSwapCoin coin = from_string(coinName);

        if (coin == AtomicSwapCoin::Unknown)
        {
            throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, "Unknown coin.");
        }

        RecommendedFeeRate data{ coin };
        return std::make_pair(data, MethodInfo());
    }

    std::pair<CancelOffer, IWalletApi::MethodInfo> WalletApi::onParseCancelOffer(const JsonRpcId& id, const json& params)
    {
        auto txcRes = onParseTxCancel(id, params);
        return std::make_pair(CancelOffer{txcRes.first}, txcRes.second);
    }

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
                {"txId", std::to_string(res.txId)},
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

    void WalletApi::getResponse(const JsonRpcId& id, const CancelOffer::Response& res, json& msg)
    {
        TxCancel::Response cancelRes{res.result};
        getResponse(id, cancelRes, msg);
    }
}
