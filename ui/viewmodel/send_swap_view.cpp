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
    : _sendAmount(0)
    , _sendFee(0)
    , _sendCurrency(Currency::CurrBeam)
    , _receiveAmount(0)
    , _receiveFee(0)
    , _receiveCurrency(Currency::CurrBtc)
    , _change(0)
    , _walletModel(*AppModel::getInstance().getWallet())
{
    LOG_INFO() << "SendSwapViewModel created";
    connect(&_walletModel, &WalletModel::changeCalculated,  this,  &SendSwapViewModel::onChangeCalculated);
    //connect(&_walletModel, &WalletModel::sendMoneyVerified,      this,  &SendViewModel::onSendMoneyVerified);
    //connect(&_walletModel, &WalletModel::cantSendToExpired,      this,  &SendViewModel::onCantSendToExpired);

    _status.setOnChanged([this]() {
        recalcAvailable();
    });

    _status.refresh();
}

SendSwapViewModel::~SendSwapViewModel()
{
    LOG_INFO() << "SendSwapViewModel destroyed";
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

void SendSwapViewModel::fillParameters(beam::wallet::TxParameters parameters)
{
    // Set currency before fee, otherwise it would be reset to default fee
    using namespace beam::wallet;
    using namespace beam;

    auto isBeamSide = parameters.GetParameter<bool>(TxParameterID::AtomicSwapIsBeamSide);
    auto swapCoin = parameters.GetParameter<AtomicSwapCoin>(TxParameterID::AtomicSwapCoin);
    auto beamAmount = parameters.GetParameter<Amount>(TxParameterID::Amount);
    auto swapAmount = parameters.GetParameter<Amount>(TxParameterID::AtomicSwapAmount);
    auto peerID = parameters.GetParameter<WalletID>(TxParameterID::PeerID);
    auto peerResponseHeight = parameters.GetParameter<Height>(TxParameterID::PeerResponseHeight);

    if (peerID && swapAmount && beamAmount
        && swapCoin && isBeamSide && peerResponseHeight)
    {
        if (*isBeamSide) // other participant is not a beam side
        {
            // Do not set fee, it is set automatically based on the currency param
            setSendCurrency(Currency::CurrBeam);
            setSendAmount(double(*beamAmount) / Rules::Coin);
            setReceiveCurrency(convertSwapCoinToCurrency(*swapCoin));
            setReceiveAmount(double(*swapAmount) / UnitsPerCoin(*swapCoin));
        }
        else
        {
            // Do not set fee, it is set automatically based on the currency param
            setSendCurrency(convertSwapCoinToCurrency(*swapCoin));
            setSendAmount(double(*swapAmount) / UnitsPerCoin(*swapCoin));
            setReceiveCurrency(Currency::CurrBeam);
            setReceiveAmount(double(*beamAmount) / Rules::Coin);
        }
        setOfferedTime(QDateTime::currentDateTime()); // TODO:SWAP use peerResponseHeight
        setExpiresTime(QDateTime::currentDateTime().addSecs(12*3600)); //
        _txParameters = parameters;
    }
}

void SendSwapViewModel::setParameters(QVariant parameters)
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
        emit tokenChanged();
        auto parameters = beam::wallet::ParseParameters(_token.toStdString());
        if (getTokenValid() && parameters)
        {
            fillParameters(parameters.value());
        }
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

double SendSwapViewModel::getSendAmount() const
{
    return _sendAmount;
}

void SendSwapViewModel::setSendAmount(double value)
{
    LOG_INFO() << "setSendAmount " << value;
    if (value != _sendAmount)
    {
        _sendAmount = value;
        emit sendAmountChanged();
        recalcAvailable();
    }
}

int SendSwapViewModel::getSendFee() const
{
    return _sendFee;
}

void SendSwapViewModel::setSendFee(int value)
{
    LOG_INFO() << "setSendFee " << value;
    if (value != _sendFee)
    {
        _sendFee = value;
        emit sendFeeChanged();
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
    LOG_INFO() << "setSendCurrency " << static_cast<int>(value);

    if (value != _sendCurrency)
    {
        _sendCurrency = value;
        emit sendCurrencyChanged();
        recalcAvailable();
    }
}

double SendSwapViewModel::getReceiveAmount() const
{
    return _receiveAmount;
}

void SendSwapViewModel::setReceiveAmount(double value)
{
    LOG_INFO() << "setReceiveAmount " << value;
    if (value != _receiveAmount)
    {
        _receiveAmount = value;
        emit receiveAmountChanged();
    }
}

int SendSwapViewModel::getReceiveFee() const
{
    return _receiveFee;
}

void SendSwapViewModel::setReceiveFee(int value)
{
    LOG_INFO() << "setReceiveFee " << value;
    if (value != _receiveFee)
    {
        _receiveFee = value;
        emit receiveFeeChanged();
    }
}

Currency SendSwapViewModel::getReceiveCurrency() const
{
    return _receiveCurrency;
}

void SendSwapViewModel::setReceiveCurrency(Currency value)
{
    assert(value > Currency::CurrStart && value < Currency::CurrEnd);
    LOG_INFO() << "setReceiveCurrency " << static_cast<int>(value);

    if (value != _receiveCurrency)
    {
        _receiveCurrency = value;
        emit receiveCurrencyChanged();
    }
}

QString SendSwapViewModel::getComment() const
{
    return _comment;
}

void SendSwapViewModel::setComment(const QString& value)
{
    LOG_INFO() << "setComment " << value.toStdString();

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
    LOG_INFO() << "setOfferedTime " << value.toString().toStdString();
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
    LOG_INFO() << "setExpiresTime " << value.toString().toStdString();
    if (_expiresTime != value)
    {
        _expiresTime = value;
        emit expiresTimeChanged();
    }
}

void SendSwapViewModel::onChangeCalculated(beam::Amount change)
{
    _change = change;
    emit enoughChanged();
    emit canSendChanged();
}

bool SendSwapViewModel::isEnough() const
{
    switch(_sendCurrency)
    {
    case Currency::CurrBeam:
    {
        auto total = std::round(_sendAmount * beam::Rules::Coin) + _sendFee + _change;
        return _status.getAvailable() >= total;
    }
    case Currency::CurrBtc:
    {
        auto total = _sendAmount + double(_sendFee) / beam::wallet::UnitsPerCoin(beam::wallet::AtomicSwapCoin::Bitcoin);
        return AppModel::getInstance().getBitcoinClient()->getAvailable() > total;
    }
    case Currency::CurrLtc:
    {
        auto total = _sendAmount + double(_sendFee) / beam::wallet::UnitsPerCoin(beam::wallet::AtomicSwapCoin::Litecoin);
        return AppModel::getInstance().getLitecoinClient()->getAvailable() > total;
    }
    case Currency::CurrQtum:
    {
        auto total = _sendAmount + double(_sendFee) / beam::wallet::UnitsPerCoin(beam::wallet::AtomicSwapCoin::Qtum);
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
        _change = 0;
        _walletModel.getAsync()->calcChange(std::round(_sendAmount * beam::Rules::Coin) + _sendFee);
        return;
    default:
        // TODO:SWAP implement for all currencies
        //assert(false);
        _change = 0;
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
    return QMLGlobals::isFeeOK(_sendFee, _sendCurrency) &&
           _sendCurrency != _receiveCurrency &&
           isEnough() &&
           QDateTime::currentDateTime() < _expiresTime;
}

void SendSwapViewModel::sendMoney()
{
    auto txParameters = beam::wallet::TxParameters(_txParameters);
    auto isBeamSide = txParameters.GetParameter<bool>(beam::wallet::TxParameterID::AtomicSwapIsBeamSide);
    auto beamFee = (*isBeamSide) ? getSendFee() : getReceiveFee();
    auto swapFee = (*isBeamSide) ? getReceiveFee() : getSendFee();
    auto subTxID = isBeamSide ? beam::wallet::SubTxIndex::REDEEM_TX : beam::wallet::SubTxIndex::LOCK_TX;

    txParameters.SetParameter(beam::wallet::TxParameterID::Fee, beam::Amount(beamFee));
    txParameters.SetParameter(beam::wallet::TxParameterID::Fee, beam::Amount(swapFee), subTxID);

    _walletModel.getAsync()->startTransaction(beam::wallet::TxParameters(txParameters));
}