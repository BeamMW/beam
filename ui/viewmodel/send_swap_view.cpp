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
#include "send_swap_view.h"
#include "model/app_model.h"
#include "qml_globals.h"
#include "wallet/swaps/common.h"
#include "ui_helpers.h"

SendSwapViewModel::SendSwapViewModel()
    : _sendAmountGrothes(0)
    , _sendFeeGrothes(0)
    , _sendCurrency(Currency::CurrBeam)
    , _receiveAmountGrothes(0)
    , _receiveFeeGrothes(0)
    , _receiveCurrency(Currency::CurrBtc)
    , _changeGrothes(0)
    , _walletModel(*AppModel::getInstance().getWallet())
    , _isBeamSide(true)
{
    connect(&_walletModel, &WalletModel::changeCalculated,  this,  &SendSwapViewModel::onChangeCalculated);
    connect(&_walletModel, &WalletModel::availableChanged, this, &SendSwapViewModel::recalcAvailable);
}

QString SendSwapViewModel::getToken() const
{
    return _token;
}

namespace
{
    Currency convertSwapCoinToCurrency(beam::wallet::AtomicSwapCoin  coin)
    {
        switch (coin)
        {
        case beam::wallet::AtomicSwapCoin::Bitcoin:
            return Currency::CurrBtc;
        case beam::wallet::AtomicSwapCoin::Litecoin:
            return Currency::CurrLtc;
        case beam::wallet::AtomicSwapCoin::Qtum:
            return Currency::CurrQtum;
        default:
            return Currency::CurrBeam;
        }
    }
}

void SendSwapViewModel::fillParameters(const beam::wallet::TxParameters& parameters)
{
    // Set currency before fee, otherwise it would be reset to default fee
    using namespace beam::wallet;
    using namespace beam;

    auto isBeamSide = parameters.GetParameter<bool>(TxParameterID::AtomicSwapIsBeamSide);
    auto swapCoin = parameters.GetParameter<AtomicSwapCoin>(TxParameterID::AtomicSwapCoin);
    auto beamAmount = parameters.GetParameter<Amount>(TxParameterID::Amount);
    auto swapAmount = parameters.GetParameter<Amount>(TxParameterID::AtomicSwapAmount);
    auto peerID = parameters.GetParameter<WalletID>(TxParameterID::PeerID);
    auto peerResponseTime = parameters.GetParameter<Height>(TxParameterID::PeerResponseTime);
    auto offeredTime = parameters.GetParameter<Timestamp>(TxParameterID::CreateTime);
    auto minHeight = parameters.GetParameter<Height>(TxParameterID::MinHeight);

    if (peerID && swapAmount && beamAmount && swapCoin && isBeamSide
        && peerResponseTime && offeredTime && minHeight)
    {
        if (*isBeamSide) // other participant is not a beam side
        {
            // Do not set fee, it is set automatically based on the currency param
            setSendCurrency(Currency::CurrBeam);
            setSendAmount(beamui::AmountToUIString(*beamAmount));
            setReceiveCurrency(convertSwapCoinToCurrency(*swapCoin));
            setReceiveAmount(beamui::AmountToUIString(*swapAmount));
        }
        else
        {
            // Do not set fee, it is set automatically based on the currency param
            setSendCurrency(convertSwapCoinToCurrency(*swapCoin));
            setSendAmount(beamui::AmountToUIString(*swapAmount));
            setReceiveCurrency(Currency::CurrBeam);
            setReceiveAmount(beamui::AmountToUIString(*beamAmount));
        }
        setOfferedTime(QDateTime::fromSecsSinceEpoch(*offeredTime));

        auto currentHeight = _walletModel.getCurrentHeight();
        assert(currentHeight);
        auto expiresHeight = *minHeight + *peerResponseTime;
        setExpiresTime(beamui::CalculateExpiresTime(currentHeight, expiresHeight));

        _txParameters = parameters;
        _isBeamSide = *isBeamSide;
    }
}

void SendSwapViewModel::setParameters(const QVariant& parameters)
{
    if (!parameters.isNull() && parameters.isValid())
    {
        auto p = parameters.value<beam::wallet::TxParameters>();
        fillParameters(p);
    }
}

void SendSwapViewModel::setToken(const QString& value)
{
    if (_token != value)
    {
        _token = value;
        auto parameters = beam::wallet::ParseParameters(_token.toStdString());
        if (getTokenValid() && parameters)
        {
            fillParameters(parameters.value());
        }
        emit tokenChanged();
    }
}

bool SendSwapViewModel::getTokenValid() const
{
    if (QMLGlobals::isSwapToken(_token))
    {
        // TODO:SWAP check if token is valid
        return true;
    }

    return false;
}

bool SendSwapViewModel::getParametersValid() const
{
    auto type = _txParameters.GetParameter<beam::wallet::TxType>(beam::wallet::TxParameterID::TransactionType);
    return type && *type == beam::wallet::TxType::AtomicSwap;
}

QString SendSwapViewModel::getSendAmount() const
{
    return beamui::AmountToUIString(_sendAmountGrothes);
}

void SendSwapViewModel::setSendAmount(QString value)
{
    const auto amount = beamui::UIStringToAmount(value);
    if (amount != _sendAmountGrothes)
    {
        _sendAmountGrothes = amount;
        emit sendAmountChanged();
        emit isSendFeeOKChanged();
        recalcAvailable();
    }
}

unsigned int SendSwapViewModel::getSendFee() const
{
    return _sendFeeGrothes;
}

void SendSwapViewModel::setSendFee(unsigned int value)
{
    if (value != _sendFeeGrothes)
    {
        _sendFeeGrothes = value;
        emit sendFeeChanged();
        emit isSendFeeOKChanged();
        recalcAvailable();
    }
}

Currency SendSwapViewModel::getSendCurrency() const
{
    return _sendCurrency;
}

void SendSwapViewModel::setSendCurrency(Currency value)
{
    assert(value > Currency::CurrStart && value < Currency::CurrEnd);

    if (value != _sendCurrency)
    {
        _sendCurrency = value;
        emit sendCurrencyChanged();
        emit isSendFeeOKChanged();
        recalcAvailable();
    }
}

QString SendSwapViewModel::getReceiveAmount() const
{
    return beamui::AmountToUIString(_receiveAmountGrothes);
}

void SendSwapViewModel::setReceiveAmount(QString value)
{
    const auto amount = beamui::UIStringToAmount(value);
    if (amount != _receiveAmountGrothes)
    {
        _receiveAmountGrothes = amount;
        emit receiveAmountChanged();
        emit isReceiveFeeOKChanged();
    }
}

unsigned int SendSwapViewModel::getReceiveFee() const
{
    return _receiveFeeGrothes;
}

void SendSwapViewModel::setReceiveFee(unsigned int value)
{
    if (value != _receiveFeeGrothes)
    {
        _receiveFeeGrothes = value;
        emit receiveFeeChanged();
        emit canSendChanged();
        emit isReceiveFeeOKChanged();
    }
}

Currency SendSwapViewModel::getReceiveCurrency() const
{
    return _receiveCurrency;
}

void SendSwapViewModel::setReceiveCurrency(Currency value)
{
    assert(value > Currency::CurrStart && value < Currency::CurrEnd);

    if (value != _receiveCurrency)
    {
        _receiveCurrency = value;
        emit receiveCurrencyChanged();
        emit isReceiveFeeOKChanged();
    }
}

QString SendSwapViewModel::getComment() const
{
    return _comment;
}

void SendSwapViewModel::setComment(const QString& value)
{
    if (_comment != value)
    {
        _comment = value;
        emit commentChanged();
    }
}

QDateTime SendSwapViewModel::getOfferedTime() const
{
    return _offeredTime;
}

void SendSwapViewModel::setOfferedTime(const QDateTime& value)
{
    if (_offeredTime != value)
    {
        _offeredTime = value;
        emit offeredTimeChanged();
    }
}

QDateTime SendSwapViewModel::getExpiresTime() const
{
    return _expiresTime;
}

void SendSwapViewModel::setExpiresTime(const QDateTime& value)
{
    if (_expiresTime != value)
    {
        _expiresTime = value;
        emit expiresTimeChanged();
    }
}

void SendSwapViewModel::onChangeCalculated(beam::Amount change)
{
    _changeGrothes = change;
    emit enoughChanged();
    emit canSendChanged();
}

bool SendSwapViewModel::isEnough() const
{
    switch(_sendCurrency)
    {
    case Currency::CurrBeam:
    {
        const auto total = _sendAmountGrothes + _sendFeeGrothes + _changeGrothes;
        return _walletModel.getAvailable() >= total;
    }
    case Currency::CurrBtc:
    {
        // TODO sentFee is fee rate. should be corrected
        const beam::Amount total = _sendAmountGrothes + _sendFeeGrothes;
        return AppModel::getInstance().getBitcoinClient()->getAvailable() > total;
    }
    case Currency::CurrLtc:
    {
        const beam::Amount total = _sendAmountGrothes + _sendFeeGrothes;
        return AppModel::getInstance().getLitecoinClient()->getAvailable() > total;
    }
    case Currency::CurrQtum:
    {
        const beam::Amount total = _sendAmountGrothes + _sendFeeGrothes;
        return AppModel::getInstance().getQtumClient()->getAvailable() > total;
    }
    default:
    {
        assert(false);
        return true;
    }
    }
}

void SendSwapViewModel::recalcAvailable()
{
    switch(_sendCurrency)
    {
    case Currency::CurrBeam:
        _changeGrothes = 0;
        _walletModel.getAsync()->calcChange(_sendAmountGrothes + _sendFeeGrothes);
        return;
    default:
        // TODO:SWAP implement for all currencies
        _changeGrothes = 0;
    }

    emit enoughChanged();
    emit canSendChanged();
}

QString SendSwapViewModel::getReceiverAddress() const
{
    auto peerID = _txParameters.GetParameter<beam::wallet::WalletID>(beam::wallet::TxParameterID::PeerID);
    if (peerID)
    {
        return beamui::toString(*peerID);
    }
    return _token;
}

bool SendSwapViewModel::canSend() const
{
    // TODO:SWAP check if correct
    return isSendFeeOK() && isReceiveFeeOK() &&
           _sendCurrency != _receiveCurrency &&
           isEnough() &&
           QDateTime::currentDateTime() < _expiresTime;
}

void SendSwapViewModel::sendMoney()
{
    using beam::wallet::TxParameterID;
    
    auto txParameters = beam::wallet::TxParameters(_txParameters);
    auto beamFee = _isBeamSide ? getSendFee() : getReceiveFee();
    auto swapFee = _isBeamSide ? getReceiveFee() : getSendFee();

    if (_isBeamSide)
    {
        txParameters.SetParameter(TxParameterID::Fee, beam::Amount(beamFee), beam::wallet::SubTxIndex::BEAM_LOCK_TX);
        txParameters.SetParameter(TxParameterID::Fee, beam::Amount(beamFee), beam::wallet::SubTxIndex::BEAM_REFUND_TX);
        txParameters.SetParameter(TxParameterID::Fee, beam::Amount(swapFee), beam::wallet::SubTxIndex::REDEEM_TX);
    }
    else
    {
        txParameters.SetParameter(TxParameterID::Fee, beam::Amount(swapFee), beam::wallet::SubTxIndex::LOCK_TX);
        txParameters.SetParameter(TxParameterID::Fee, beam::Amount(swapFee), beam::wallet::SubTxIndex::REFUND_TX);
        txParameters.SetParameter(TxParameterID::Fee, beam::Amount(beamFee), beam::wallet::SubTxIndex::BEAM_REDEEM_TX);
    }

    if (!_comment.isEmpty())
    {
        std::string localComment = _comment.toStdString();
        txParameters.SetParameter(TxParameterID::Message, beam::ByteBuffer(localComment.begin(), localComment.end()));
    }

    {
        auto txID = txParameters.GetTxID();
        auto swapCoin = txParameters.GetParameter<beam::wallet::AtomicSwapCoin>(TxParameterID::AtomicSwapCoin);
        auto amount = txParameters.GetParameter<beam::Amount>(TxParameterID::Amount);
        auto swapAmount = txParameters.GetParameter<beam::Amount>(TxParameterID::AtomicSwapAmount);
        auto responseHeight = txParameters.GetParameter<beam::Height>(TxParameterID::PeerResponseTime);
        auto minimalHeight = txParameters.GetParameter<beam::Height>(TxParameterID::MinHeight);

        LOG_INFO() << *txID << " Accept offer.\n\t"
                    << "isBeamSide: " << (_isBeamSide ? "true" : "false") << "\n\t"
                    << "swapCoin: " << std::to_string(*swapCoin) << "\n\t"
                    << "amount: " << *amount << "\n\t"
                    << "swapAmount: " << *swapAmount << "\n\t"
                    << "responseHeight: " << *responseHeight << "\n\t"
                    << "minimalHeight: " << *minimalHeight;
    }

    _walletModel.getAsync()->startTransaction(std::move(txParameters));
}

bool SendSwapViewModel::isSendFeeOK() const
{
    return _sendAmountGrothes == 0 || QMLGlobals::isSwapFeeOK(_sendAmountGrothes, _sendFeeGrothes, _sendCurrency);
}

bool SendSwapViewModel::isReceiveFeeOK() const
{
    return _receiveAmountGrothes == 0 || QMLGlobals::isSwapFeeOK(_receiveAmountGrothes, _receiveFeeGrothes, _receiveCurrency);
}

bool SendSwapViewModel::isSendBeam() const
{
    return _isBeamSide;
}

QString SendSwapViewModel::getRate() const
{
    beam::Amount otherCoinAmount =
        isSendBeam() ? _receiveAmountGrothes : _sendAmountGrothes;
    beam::Amount beamAmount =
        isSendBeam() ? _sendAmountGrothes : _receiveAmountGrothes;

    if (!beamAmount) return QString();

    return QMLGlobals::divideWithPrecision8(beamui::AmountToUIString(otherCoinAmount), beamui::AmountToUIString(beamAmount));
}
