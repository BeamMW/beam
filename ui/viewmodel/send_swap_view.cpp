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

SendSwapViewModel::SendSwapViewModel()
    : _sendAmount(0)
    , _sendFee(0)
    , _sendCurrency(Currency::CurrBEAM)
    , _receiveAmount(0)
    , _receiveFee(0)
    , _receiveCurrency(Currency::CurrBTC)
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

void SendSwapViewModel::setToken(const QString& value)
{
    if (_token != value)
    {
        _token = value;
        emit tokenChanged();
        if (getTokenValid())
        {
            // TODO:SWAP Parse and set real values
            // Set currency before fee, otherwise it would be reset to default fee
            setSendCurrency(Currency::CurrBEAM);
            setSendAmount(40);
            setSendFee(110);
            setReceiveCurrency(Currency::CurrLTC);
            setReceiveAmount(0.6);
            setReceiveFee(95000);
            setOfferedTime(QDateTime::currentDateTime());
            setExpiresTime(QDateTime::currentDateTime());
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
        setToken("112233");
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
    case Currency::CurrBEAM:
        {
            auto total = std::round(_sendAmount * beam::Rules::Coin) + _sendFee + _change;
            return _status.getAvailable() >= total;
        }
    default:
        // TODO:SWAP implement for all currencies
        assert(false);
        return true;
    }
}

void SendSwapViewModel::recalcAvailable()
{
    switch(_sendCurrency)
    {
    case Currency::CurrBEAM:
        _change = 0;
        _walletModel.getAsync()->calcChange(std::round(_sendAmount * beam::Rules::Coin) + _sendFee);
        return;
    default:
        // TODO:SWAP implement for all currencies
        assert(false);
        _change = 0;
    }

    emit enoughChanged();
    emit canSendChanged();
}

QString SendSwapViewModel::getReceiverAddress() const
{
    // TODO:SWAP return extracted address if we have token.
    // Now we return token, just for tests. Need to return real addres. It is used in confirmation dialog
    return _token;
}

bool SendSwapViewModel::canSend() const
{
    // TODO:SWAP check if correct
    return QMLGlobals::isSwapToken(_token) &&
           QMLGlobals::isFeeOK(_sendFee, _sendCurrency) &&
           _sendCurrency != _receiveCurrency &&
           isEnough() &&
           QDateTime::currentDateTime() < _expiresTime;
}

void SendSwapViewModel::sendMoney()
{
    // TODO:SWAP implement
}