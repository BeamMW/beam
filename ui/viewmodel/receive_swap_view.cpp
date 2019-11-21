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

#include "receive_swap_view.h"
#include "ui_helpers.h"
#include "model/app_model.h"
#include "wallet/swaps/utils.h"
#include "wallet/bitcoin/bitcoin_side.h"
#include "wallet/litecoin/litecoin_side.h"
#include "wallet/qtum/qtum_side.h"
#include <QClipboard>
#include "qml_globals.h"

namespace {
    enum {
        OfferExpires12h = 0,
        OfferExpires6h  = 1
    };

    uint16_t GetHourCount(int offerExpires)
    {
        switch (offerExpires)
        {
        case OfferExpires12h:
            return 12;
        case OfferExpires6h:
            return 6;
        default:
        {
            assert(false && "Unexpected value!");
            return 0;
        }
        }
    }

    beam::Height GetBlockCount(int offerExpires)
    {
        return GetHourCount(offerExpires) * 60;
    }
}

ReceiveSwapViewModel::ReceiveSwapViewModel()
    : _amountToReceiveGrothes(0)
    , _amountSentGrothes(0)
    , _receiveFeeGrothes(0)
    , _sentFeeGrothes(0)
    , _receiveCurrency(Currency::CurrBeam)
    , _sentCurrency(Currency::CurrBtc)
    , _offerExpires(OfferExpires12h)
    , _saveParamsAllowed(false)
    , _walletModel(*AppModel::getInstance().getWallet())
    , _txParameters(beam::wallet::CreateSwapParameters()
        .SetParameter(beam::wallet::TxParameterID::AtomicSwapCoin, beam::wallet::AtomicSwapCoin::Bitcoin)
        .SetParameter(beam::wallet::TxParameterID::AtomicSwapIsBeamSide, true)
        .SetParameter(beam::wallet::TxParameterID::IsInitiator, true))
{
    connect(&_walletModel, &WalletModel::generatedNewAddress, this, &ReceiveSwapViewModel::onGeneratedNewAddress);
    connect(&_walletModel, &WalletModel::swapParamsLoaded, this, &ReceiveSwapViewModel::onSwapParamsLoaded);
    connect(&_walletModel, SIGNAL(newAddressFailed()), this, SIGNAL(newAddressFailed()));
    connect(&_walletModel, &WalletModel::stateIDChanged, this, &ReceiveSwapViewModel::updateTransactionToken);

    generateNewAddress();
    updateTransactionToken();
}

void ReceiveSwapViewModel::onGeneratedNewAddress(const beam::wallet::WalletAddress& addr)
{
    _receiverAddress = addr;
    emit receiverAddressChanged();
    updateTransactionToken();
    loadSwapParams();
}

void ReceiveSwapViewModel::loadSwapParams()
{
    _walletModel.getAsync()->loadSwapParams();
}

void ReceiveSwapViewModel::storeSwapParams()
{
    if(!_saveParamsAllowed) return;

    try {
        beam::Serializer ser;
        ser & _receiveCurrency
            & _sentCurrency
            & _receiveFeeGrothes
            & _sentFeeGrothes;

        beam::ByteBuffer buffer;
        ser.swap_buf(buffer);
        _walletModel.getAsync()->storeSwapParams(buffer);
    }
    catch(...)
    {
        LOG_ERROR() << "failed to serialize swap params";
    }
}

void ReceiveSwapViewModel::onSwapParamsLoaded(const beam::ByteBuffer& params)
{
    if(!params.empty())
    {
        try
        {
            beam::Deserializer der;
            der.reset(params);

            Currency receiveCurrency, sentCurrency;
            beam::Amount receiveFee, sentFee;

            der & receiveCurrency
                & sentCurrency
                & receiveFee
                & sentFee;

            setReceiveCurrency(receiveCurrency);
            setSentCurrency(sentCurrency);
            setReceiveFee(receiveFee);
            setSentFee(sentFee);
        }
        catch(...)
        {
            LOG_ERROR() << "failed to deserialize swap params";
        }
    }

   _saveParamsAllowed = true;
}

QString ReceiveSwapViewModel::getAmountToReceive() const
{
    return beamui::AmountToUIString(_amountToReceiveGrothes);
}

void ReceiveSwapViewModel::setAmountToReceive(QString value)
{
    auto amount = beamui::UIStringToAmount(value);
    if (amount != _amountToReceiveGrothes)
    {
        _amountToReceiveGrothes = amount;
        emit amountToReceiveChanged();
        updateTransactionToken();
    }
}

QString ReceiveSwapViewModel::getAmountSent() const
{
    return beamui::AmountToUIString(_amountSentGrothes);
}

unsigned int ReceiveSwapViewModel::getReceiveFee() const
{
    return _receiveFeeGrothes;
}

void ReceiveSwapViewModel::setAmountSent(QString value)
{
    auto amount = beamui::UIStringToAmount(value);
    if (amount != _amountSentGrothes)
    {
        _amountSentGrothes = amount;
        emit amountSentChanged();
        updateTransactionToken();
    }
}

unsigned int ReceiveSwapViewModel::getSentFee() const
{
    return _sentFeeGrothes;
}

void ReceiveSwapViewModel::setSentFee(unsigned int value)
{
    if (value != _sentFeeGrothes)
    {
        _sentFeeGrothes = value;
        emit sentFeeChanged();
        storeSwapParams();
    }
}

Currency ReceiveSwapViewModel::getReceiveCurrency() const
{
    return _receiveCurrency;
}

void ReceiveSwapViewModel::setReceiveCurrency(Currency value)
{
    assert(value > Currency::CurrStart && value < Currency::CurrEnd);

    if (value != _receiveCurrency)
    {
        _receiveCurrency = value;
        emit receiveCurrencyChanged();
        updateTransactionToken();
        storeSwapParams();
    }
}

Currency ReceiveSwapViewModel::getSentCurrency() const
{
    return _sentCurrency;
}

void ReceiveSwapViewModel::setSentCurrency(Currency value)
{
    assert(value > Currency::CurrStart && value < Currency::CurrEnd);

    if (value != _sentCurrency)
    {
        _sentCurrency = value;
        emit sentCurrencyChanged();
        updateTransactionToken();
        storeSwapParams();
    }
}

void ReceiveSwapViewModel::setReceiveFee(unsigned int value)
{
    if (value != _receiveFeeGrothes)
    {
        _receiveFeeGrothes = value;
        emit receiveFeeChanged();
        storeSwapParams();
    }
}

int ReceiveSwapViewModel::getOfferExpires() const
{
    return _offerExpires;
}

void ReceiveSwapViewModel::setOfferExpires(int value)
{
    if (value != _offerExpires)
    {
        _offerExpires = value;
        emit offerExpiresChanged();
        updateTransactionToken();
    }
}

QString ReceiveSwapViewModel::getReceiverAddress() const
{
    return beamui::toString(_receiverAddress.m_walletID);
}

void ReceiveSwapViewModel::generateNewAddress()
{
    _receiverAddress = {};
    emit receiverAddressChanged();

    setAddressComment("");
    _walletModel.getAsync()->generateNewAddress();
}

void ReceiveSwapViewModel::setTransactionToken(const QString& value)
{
    if (_token != value)
    {
        _token = value;
        emit transactionTokenChanged();
    }
}

QString ReceiveSwapViewModel::getTransactionToken() const
{
    return _token;
}

QString ReceiveSwapViewModel::getAddressComment() const
{
    return _addressComment;
}

void ReceiveSwapViewModel::setAddressComment(const QString& value)
{
    auto trimmed = value.trimmed();
    if (_addressComment != trimmed)
    {
        _addressComment = trimmed;
        emit addressCommentChanged();
        emit commentValidChanged();
    }
}

bool ReceiveSwapViewModel::getCommentValid() const
{
    return !_walletModel.isAddressWithCommentExist(_addressComment.toStdString());
}

bool ReceiveSwapViewModel::isEnough() const
{
    if (_amountSentGrothes == 0)
        return true;

    switch (_sentCurrency)
    {
    case Currency::CurrBeam:
    {
        auto total = _amountSentGrothes + _sentFeeGrothes;
        return _walletModel.getAvailable() >= total;
    }
    case Currency::CurrBtc:
    {
        // TODO sentFee is fee rate. should be corrected
        beam::Amount total = _amountSentGrothes + _sentFeeGrothes;
        return AppModel::getInstance().getBitcoinClient()->getAvailable() > total;
    }
    case Currency::CurrLtc:
    {
        beam::Amount total = _amountSentGrothes + _sentFeeGrothes;
        return AppModel::getInstance().getLitecoinClient()->getAvailable() > total;
    }
    case Currency::CurrQtum:
    {
        beam::Amount total = _amountSentGrothes + _sentFeeGrothes;
        return AppModel::getInstance().getQtumClient()->getAvailable() > total;
    }
    default:
    {
        assert(false);
        return true;
    }
    }
}

bool ReceiveSwapViewModel::isSendFeeOK() const
{
    return _amountSentGrothes == 0 || QMLGlobals::isSwapFeeOK(_amountSentGrothes, _sentFeeGrothes, _sentCurrency);
}

bool ReceiveSwapViewModel::isReceiveFeeOK() const
{
    return _amountToReceiveGrothes == 0 || QMLGlobals::isSwapFeeOK(_amountToReceiveGrothes, _receiveFeeGrothes, _receiveCurrency);
}

void ReceiveSwapViewModel::saveAddress()
{
    using namespace beam::wallet;

    if (getCommentValid()) {
        _receiverAddress.m_label = _addressComment.toStdString();
        _receiverAddress.m_duration = GetHourCount(_offerExpires) * WalletAddress::AddressExpiration1h;
        _walletModel.getAsync()->saveAddress(_receiverAddress, true);
    }
}

void ReceiveSwapViewModel::startListen()
{
    using namespace beam::wallet;

    bool isBeamSide = (_sentCurrency == Currency::CurrBeam);
    auto beamFee = isBeamSide ? _sentFeeGrothes : _receiveFeeGrothes;
    auto swapFee = isBeamSide ? _receiveFeeGrothes : _sentFeeGrothes;
    auto txParameters = beam::wallet::TxParameters(_txParameters);

    txParameters.DeleteParameter(TxParameterID::PeerID);
    txParameters.SetParameter(TxParameterID::IsInitiator, !*_txParameters.GetParameter<bool>(TxParameterID::IsInitiator));
    txParameters.SetParameter(TxParameterID::MyID, *_txParameters.GetParameter<beam::wallet::WalletID>(beam::wallet::TxParameterID::PeerID));
    if (isBeamSide)
    {
        txParameters.SetParameter(TxParameterID::Fee, beam::Amount(beamFee), SubTxIndex::BEAM_LOCK_TX);
        txParameters.SetParameter(TxParameterID::Fee, beam::Amount(beamFee), SubTxIndex::BEAM_REFUND_TX);
        txParameters.SetParameter(TxParameterID::Fee, beam::Amount(swapFee), SubTxIndex::REDEEM_TX);
    }
    else
    {
        txParameters.SetParameter(TxParameterID::Fee, beam::Amount(swapFee), SubTxIndex::LOCK_TX);
        txParameters.SetParameter(TxParameterID::Fee, beam::Amount(swapFee), SubTxIndex::REFUND_TX);
        txParameters.SetParameter(TxParameterID::Fee, beam::Amount(beamFee), SubTxIndex::BEAM_REDEEM_TX);
    }
    txParameters.SetParameter(TxParameterID::AtomicSwapIsBeamSide, isBeamSide);
    txParameters.SetParameter(TxParameterID::IsSender, isBeamSide);
    if (!_addressComment.isEmpty())
    {
        std::string localComment = _addressComment.toStdString();
        txParameters.SetParameter(TxParameterID::Message, beam::ByteBuffer(localComment.begin(), localComment.end()));
    }

    _walletModel.getAsync()->startTransaction(std::move(txParameters));
}

void ReceiveSwapViewModel::publishToken()
{
    auto txId = _txParameters.GetTxID();
    auto publisherId = _txParameters.GetParameter<beam::wallet::WalletID>(beam::wallet::TxParameterID::PeerID);
    auto coin = _txParameters.GetParameter<beam::wallet::AtomicSwapCoin>(beam::wallet::TxParameterID::AtomicSwapCoin);
    if (publisherId && txId && coin)
    {
        beam::wallet::SwapOffer offer(*txId);
        offer.m_txId = *txId;
        offer.m_publisherId = *publisherId;
        offer.m_status = beam::wallet::SwapOfferStatus::Pending;
        offer.m_coin = *coin;
        offer.SetTxParameters(_txParameters.Pack());
        
        _walletModel.getAsync()->publishSwapOffer(offer);
    }
}

namespace
{
    beam::wallet::AtomicSwapCoin convertCurrencyToSwapCoin(Currency currency)
    {
        switch (currency)
        {
        case Currency::CurrBtc:
            return beam::wallet::AtomicSwapCoin::Bitcoin;
        case Currency::CurrLtc:
            return beam::wallet::AtomicSwapCoin::Litecoin;
        case Currency::CurrQtum:
            return beam::wallet::AtomicSwapCoin::Qtum;
        default:
            return beam::wallet::AtomicSwapCoin::Unknown;
        }
    }
}

void ReceiveSwapViewModel::updateTransactionToken()
{
    emit enoughChanged();
    emit isSendFeeOKChanged();
    emit isReceiveFeeOKChanged();
    _txParameters.SetParameter(beam::wallet::TxParameterID::MinHeight, _walletModel.getCurrentHeight());
    _txParameters.SetParameter(beam::wallet::TxParameterID::PeerResponseTime, GetBlockCount(_offerExpires));

    // All parameters sets as if we were on the recipient side (mirrored)
    bool isBeamSide = (_receiveCurrency == Currency::CurrBeam);
    auto swapCoin   = convertCurrencyToSwapCoin(isBeamSide ? _sentCurrency : _receiveCurrency);
    auto beamAmount = isBeamSide ? _amountToReceiveGrothes : _amountSentGrothes;
    auto swapAmount = isBeamSide ? _amountSentGrothes : _amountToReceiveGrothes;

    _txParameters.SetParameter(beam::wallet::TxParameterID::AtomicSwapIsBeamSide, isBeamSide);
    _txParameters.SetParameter(beam::wallet::TxParameterID::Amount, beamAmount);
    _txParameters.SetParameter(beam::wallet::TxParameterID::AtomicSwapCoin, swapCoin);
    _txParameters.SetParameter(beam::wallet::TxParameterID::AtomicSwapAmount, swapAmount);
    _txParameters.SetParameter(beam::wallet::TxParameterID::PeerID, _receiverAddress.m_walletID);
    _txParameters.SetParameter(beam::wallet::TxParameterID::IsSender, isBeamSide);

    setTransactionToken(QString::fromStdString(std::to_string(_txParameters)));
}
