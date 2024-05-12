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
#include "v6_api.h"
#include "wallet/core/common_utils.h"
#include "bvm/ManagerStd.h"
#include "wallet/client/extensions/offers_board/swap_offers_board.h"
#include "wallet/client/extensions/offers_board/swap_offer_token.h"
#include "wallet/transactions/swaps/swap_transaction.h"
#include "wallet/transactions/swaps/swap_tx_description.h"
#include "wallet/transactions/swaps/utils.h"
#include <regex>
#include "wallet/client/extensions/offers_board/swap_offers_board.h"

namespace beam::wallet
{
    const char kSwapAmountToLowError[]          = "The swap amount must be greater than the redemption fee.";
    const char kBeamAmountToLowError[]          = R"('beam_amount' must be greater than "beam_fee".)";
    const char kSwapNotEnoughtSwapCoins[]       = "There is not enough funds to complete the transaction.";
    const char kSwapFeeToLowRecommenededError[] = "\'fee_rate\' must be greater or equal than recommended fee rate.";
    const char kSwapFeeToLowError[]             = R"("'fee_rate' must be greater or equal than ")";
    const char kSwapFeeToHighError[]            = "\'fee_rate\' must be less or equal than ";

    void ensureBEAMAmount(const IWalletDB::Ptr& walletDB, Amount beamAmount, Amount beamFee)
    {
        storage::Totals allTotals(*walletDB, false);

        const auto& totals = allTotals.GetBeamTotals();
        const auto available = AmountBig::get_Lo(totals.Avail) + AmountBig::get_Lo(totals.AvailShielded);

        if (beamAmount + beamFee > available)
        {
            throw jsonrpc_exception(ApiError::SwapNotEnoughtBeams);
        }
    }

    bool checkIsEnoughtSwapAmount(const ISwapsProvider& swapProvider, AtomicSwapCoin swapCoin, Amount swapAmount, Amount swapFeeRate)
    {
        beam::Amount total = swapAmount + swapFeeRate;
        return swapProvider.getCoinAvailable(swapCoin) > total;
    }

    void ensureConnected(ISwapsProvider::Ptr swaps, AtomicSwapCoin swapCoin)
    {
        if (!swaps->isCoinClientConnected(swapCoin))
        {
            const auto message = std::string("No connection with the ") + std::to_string(swapCoin) + " wallet";
            throw jsonrpc_exception(ApiError::SwapFailToConnect, message);
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

    void V6Api::onHandleOffersList(const JsonRpcId& id, OffersList&& data)
    {
        auto swaps = getSwaps();
        auto walletDB = getWalletDB();

        std::vector<SwapOffer> publicOffers = swaps->getSwapOffersBoard().getOffersList();
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

    void V6Api::onHandleOffersBoard(const JsonRpcId& id, OffersBoard&& data)
    {
        auto swaps = getSwaps();
        auto walletDB = getWalletDB();

        std::vector<SwapOffer> offers = swaps->getSwapOffersBoard().getOffersList();
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

    void V6Api::onHandleCreateOffer(const JsonRpcId& id, CreateOffer&& data)
    {
        auto swaps    = getSwaps();
        auto walletDB = getWalletDB();
        auto wallet   = getWallet();

        ensureConnected(swaps, data.swapCoin);

        // TODO need to unite with AcceptOffer
        Amount recommendedFeeRate = swaps->getRecommendedFeeRate(data.swapCoin);

        if (recommendedFeeRate > 0 && data.swapFeeRate < recommendedFeeRate)
        {
            throw jsonrpc_exception(ApiError::InvalidJsonRpc, kSwapFeeToLowRecommenededError);
        }

        Amount minFeeRate = swaps->getMinFeeRate(data.swapCoin);

        if (minFeeRate > 0 && data.swapFeeRate < minFeeRate)
        {
            std::stringstream msg;
            msg << kSwapFeeToLowError << minFeeRate;
            throw jsonrpc_exception(ApiError::InvalidJsonRpc, msg.str());
        }


        Amount maxFeeRate = swaps->getMaxFeeRate(data.swapCoin);

        if (maxFeeRate > 0 && data.swapFeeRate > maxFeeRate)
        {
            std::stringstream msg;
            msg << kSwapFeeToHighError << maxFeeRate;
            throw jsonrpc_exception(ApiError::InvalidJsonRpc, msg.str());
        }

        if (data.beamAmount <= data.beamFee)
        {
            throw jsonrpc_exception(ApiError::InvalidJsonRpc, kBeamAmountToLowError);
        }

        if (!IsLockTxAmountValid(data.swapCoin, data.swapAmount, data.swapFeeRate))
        {
            throw jsonrpc_exception(ApiError::InvalidJsonRpc, kSwapAmountToLowError);
        }

        if (data.isBeamSide)
        {
            ensureBEAMAmount(walletDB, data.beamAmount, data.beamFee);
        }
        else
        {
            if(!checkIsEnoughtSwapAmount(*swaps, data.swapCoin, data.swapAmount, data.swapFeeRate))
            {
                throw jsonrpc_exception(ApiError::InvalidJsonRpc, kSwapNotEnoughtSwapCoins);
            }
        }

        auto txParameters = CreateSwapTransactionParameters();
        auto currentHeight = walletDB->getCurrentHeight();
        FillSwapTxParams(
            &txParameters,
            *walletDB,
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
        BEAM_LOG_DEBUG() << "transaction created: " << txId;

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

    void V6Api::onHandlePublishOffer(const JsonRpcId& id, PublishOffer&& data)
    {
        auto txParams = ParseParameters(data.token);
        if (!txParams)
        {
            throw jsonrpc_exception(ApiError::SwapFailToParseToken);
        }

        auto txId = txParams->GetTxID();
        if (!txId)
        {
            throw jsonrpc_exception(ApiError::SwapFailToParseToken, "bad or missing txId");
        }

        auto walletDB = getWalletDB();
        auto tx = walletDB->getTx(*txId);

        if (!tx)
        {
            throw jsonrpc_exception(ApiError::InternalErrorJsonRpc, "Transaction not found.");
        }

        const auto& mirroredTxParams = MirrorSwapTxParams(*tx);
        const auto& readyForTokenizeTxParams = PrepareSwapTxParamsForTokenization(mirroredTxParams);
        SwapOffer offer(readyForTokenizeTxParams);

        if (offer.m_status == SwapOfferStatus::Pending)
        {
            offer.m_publisherId = *offer.GetParameter<WalletID>(TxParameterID::PeerAddr);
            getSwaps()->getSwapOffersBoard().publishOffer(offer);

            doResponse(id, PublishOffer::Response
                {
                    walletDB->getAddresses(true),
                    walletDB->getCurrentHeight(),
                    offer
                });
        }
    }

    void V6Api::onHandleAcceptOffer(const JsonRpcId& id, AcceptOffer&& data)
    {
        auto txParams = ParseParameters(data.token);
        if (!txParams)
        {
            throw jsonrpc_exception(ApiError::SwapFailToParseToken);
        }

        ProcessLibraryVersion(*txParams);

        auto txId = txParams->GetTxID();
        if (!txId)
        {
            throw jsonrpc_exception(ApiError::SwapFailToParseToken, "bad or missing txId");
        }

        auto swaps = getSwaps();
        auto walletDB = getWalletDB();
        auto wallet = getWallet();
        auto publicOffer = getOfferFromBoardByTxId(swaps->getSwapOffersBoard().getOffersList(), *txId);
        auto myAddresses = walletDB->getAddresses(true);

        if (publicOffer)
        {
            // compare public offer and token
            if (!checkPublicOffer(*txParams, *publicOffer))
            {
                throw jsonrpc_exception(ApiError::InvalidJsonRpc, "Wrong offer params.");
            }

            if (storage::isMyAddress(myAddresses, publicOffer->m_publisherId))
            {
                throw jsonrpc_exception(ApiError::SwapFailToAcceptOwn);
            }
        }
        else
        {
            auto peerAddr = txParams->GetParameter<WalletID>(TxParameterID::PeerAddr);

            if (!peerAddr)
            {
                throw jsonrpc_exception(ApiError::SwapFailToParseToken, "bad or missing peer ID");
            }

            if (storage::isMyAddress(myAddresses, *peerAddr))
            {
                throw jsonrpc_exception(ApiError::SwapFailToAcceptOwn);
            }
        }

        if (auto tx = walletDB->getTx(*txId); tx)
        {
            throw jsonrpc_exception(ApiError::InvalidJsonRpc, "Offer already accepted.");
        }

        auto beamAmount = txParams->GetParameter<Amount>(TxParameterID::Amount);
        auto swapAmount = txParams->GetParameter<Amount>(TxParameterID::AtomicSwapAmount);
        auto swapCoin   = txParams->GetParameter<AtomicSwapCoin>(TxParameterID::AtomicSwapCoin);
        auto isBeamSide = txParams->GetParameter<bool>(TxParameterID::AtomicSwapIsBeamSide);
        if (!beamAmount || !swapAmount || !swapCoin || !isBeamSide)
        {
            throw jsonrpc_exception(ApiError::SwapFailToParseToken, "bad or missing amounts or coins");
        }

        Amount recommendedFeeRate = swaps->getRecommendedFeeRate(*swapCoin);

        if (recommendedFeeRate > 0 && data.swapFeeRate < recommendedFeeRate)
        {
            throw jsonrpc_exception(ApiError::InvalidJsonRpc, kSwapFeeToLowRecommenededError);
        }

        Amount minFeeRate = swaps->getMinFeeRate(*swapCoin);

        if (minFeeRate > 0 && data.swapFeeRate < minFeeRate)
        {
            std::stringstream msg;
            msg << kSwapFeeToLowError << minFeeRate;
            throw jsonrpc_exception(ApiError::InvalidJsonRpc, msg.str());
        }

        Amount maxFeeRate = swaps->getMaxFeeRate(*swapCoin);

        if (maxFeeRate > 0 && data.swapFeeRate > maxFeeRate)
        {
            std::stringstream msg;
            msg << kSwapFeeToHighError << maxFeeRate;
            throw jsonrpc_exception(ApiError::InvalidJsonRpc, msg.str());
        }

        if (*beamAmount <= data.beamFee)
        {
            throw jsonrpc_exception(ApiError::InvalidJsonRpc, kBeamAmountToLowError);
        }

        if (!IsLockTxAmountValid(*swapCoin, *swapAmount, data.swapFeeRate))
        {
            throw jsonrpc_exception(ApiError::InvalidJsonRpc, kSwapAmountToLowError);
        }

        ensureConnected(swaps, *swapCoin);

        if (*isBeamSide)
        {
            ensureBEAMAmount(walletDB, *beamAmount, data.beamFee);
        }
        else
        {
            if(!checkIsEnoughtSwapAmount(*swaps, *swapCoin, *swapAmount, data.swapFeeRate))
            {
                throw jsonrpc_exception(InvalidJsonRpc, kSwapNotEnoughtSwapCoins);
            }
        }

        SwapOffer offer = SwapOffer(*txParams);
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
            offer.DeleteParameter(TxParameterID::MyAddr);

        doResponse(
            id,
            AcceptOffer::Response
            {
                myAddresses,
                walletDB->getCurrentHeight(),
                offer
            });
    }

    void V6Api::onHandleOfferStatus(const JsonRpcId& id, OfferStatus&& data)
    {
        auto swaps = getSwaps();
        auto walletDB = getWalletDB();

        auto publicOffer = getOfferFromBoardByTxId(swaps->getSwapOffersBoard().getOffersList(), data.txId);
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
            throw jsonrpc_exception(ApiError::InternalErrorJsonRpc, "It is not my offer.");
        }

        doResponse(id, OfferStatus::Response{ walletDB->getCurrentHeight(), offer });
    }

    void V6Api::onHandleDecodeToken(const JsonRpcId& id, DecodeToken&& data)
    {
        auto txParams = ParseParameters(data.token);
        if (!txParams)
        {
            throw jsonrpc_exception(ApiError::SwapFailToParseToken);
        }

        auto txId = txParams->GetTxID();
        if (!txId)
        {
            throw jsonrpc_exception(ApiError::SwapFailToParseToken, "bad or missing txId");
        }

        auto peerAddr = txParams->GetParameter<WalletID>(TxParameterID::PeerAddr);
        if (!peerAddr)
        {
            throw jsonrpc_exception(ApiError::SwapFailToParseToken, "bad or missing peer ID");
        }

        SwapOffer offer;
        bool isMyOffer = false;
        auto walletDB = getWalletDB();
        auto myAddresses = walletDB->getAddresses(true);
        if (!storage::isMyAddress(myAddresses, *peerAddr))
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

        auto swaps = getSwaps();
        auto publicOffer = getOfferFromBoardByTxId(swaps->getSwapOffersBoard().getOffersList(), *txId);
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

    void V6Api::onHandleGetBalance(const JsonRpcId& id, GetBalance&& data)
    {
        auto swaps = getSwaps();
        ensureConnected(swaps, data.coin);

        Amount available = swaps->getCoinAvailable(data.coin);
        doResponse(id, GetBalance::Response{available});
    }

    void V6Api::onHandleRecommendedFeeRate(const JsonRpcId& id, RecommendedFeeRate&& data)
    {
        auto swaps = getSwaps();
        ensureConnected(swaps, data.coin);

        Amount feeRate = swaps->getRecommendedFeeRate(data.coin);
        doResponse(id, RecommendedFeeRate::Response{feeRate});
    }

    void V6Api::onHandleCancelOffer(const JsonRpcId& id, CancelOffer&& data)
    {
        BEAM_LOG_DEBUG() << "CancelOffer(txId = " << to_hex(data.txId.data(), data.txId.size()) << ")";
        onHandleTxCancel(id, std::move(data));
    }
}