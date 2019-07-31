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
#include <QClipboard>

namespace {
    enum {
        OfferExpires12h = 0,
        OfferExpires6h  = 1
    };
}

ReceiveSwapViewModel::ReceiveSwapViewModel()
    : _amountToReceive(0.0)
    , _amountSent(0.0)
    , _receiveFee(0)
    , _sentFee(0)
    , _receiveCurrency(Currency::CurrBEAM)
    , _sentCurrency(Currency::CurrBTC)
    , _offerExpires(OfferExpires12h)
    , _walletModel(*AppModel::getInstance().getWallet())
{
    LOG_INFO() << "ReceiveSwapViewModel created";
    connect(&_walletModel, &WalletModel::generatedNewAddress, this, &ReceiveSwapViewModel::onGeneratedNewAddress);
    connect(&_walletModel, &WalletModel::newAddressFailed, this,  &ReceiveSwapViewModel::onNewAddressFailed);
    generateNewAddress();

    // TODO: This is only for test, apply actual when ready
    _token = "176dan89jksasdg21skaw9q7g176dan89jksasdg21skaw9q7g176dan89jksasdg21skaw9q7g176dan89jksasdg21skaw9q7g176dan89jksasdg21skaw9q7g";
}

ReceiveSwapViewModel::~ReceiveSwapViewModel()
{
    LOG_INFO() << "ReceiveSwapViewModel destroyed";
}

void ReceiveSwapViewModel::onGeneratedNewAddress(const beam::wallet::WalletAddress& addr)
{
    _receiverAddress = addr;
    emit receiverAddressChanged();
}

double ReceiveSwapViewModel::getAmountToReceive() const
{
    return _amountToReceive;
}

void ReceiveSwapViewModel::setAmountToReceive(double value)
{
    LOG_INFO() << "setAmountToReceive " << value;
    if (value != _amountToReceive)
    {
        _amountToReceive = value;
        emit amountToReceiveChanged();
    }
}

double ReceiveSwapViewModel::getAmountSent() const
{
    return _amountSent;
}

int ReceiveSwapViewModel::getReceiveFee() const
{
    return _receiveFee;
}

void ReceiveSwapViewModel::setAmountSent(double value)
{
    LOG_INFO() << "setAmountSent " << value;
    if (value != _amountSent)
    {
        _amountSent = value;
        emit amountSentChanged();
    }
}

int ReceiveSwapViewModel::getSentFee() const
{
    return _sentFee;
}

void ReceiveSwapViewModel::setSentFee(int value)
{
    LOG_INFO() << "setSentFee " << value;
    if (value != _sentFee)
    {
        _sentFee = value;
        emit sentFeeChanged();
    }
}

Currency ReceiveSwapViewModel::getReceiveCurrency() const
{
    return _receiveCurrency;
}

void ReceiveSwapViewModel::setReceiveCurrency(Currency value)
{
    assert(value > Currency::CurrStart && value < Currency::CurrEnd);
    LOG_INFO() << "setReceiveCurrency " << static_cast<int>(value);

    if (value != _receiveCurrency)
    {
        _receiveCurrency = value;
        emit receiveCurrencyChanged();
    }
}

Currency ReceiveSwapViewModel::getSentCurrency() const
{
    return _sentCurrency;
}

void ReceiveSwapViewModel::setSentCurrency(Currency value)
{
    assert(value > Currency::CurrStart && value < Currency::CurrEnd);
    LOG_INFO() << "setSentCurrency " << static_cast<int>(value);

    if (value != _sentCurrency)
    {
        _sentCurrency = value;
        emit sentCurrencyChanged();
    }
}

void ReceiveSwapViewModel::setReceiveFee(int value)
{
    LOG_INFO() << "setReceiveFee " << value;
    if (value != _receiveFee)
    {
        _receiveFee = value;
        emit receiveFeeChanged();
    }
}

int ReceiveSwapViewModel::getOfferExpires() const
{
    return _offerExpires;
}

void ReceiveSwapViewModel::setOfferExpires(int value)
{
    LOG_INFO() << "setOfferExpires " << value;
    if (value != _offerExpires)
    {
        _offerExpires = value;
        emit offerExpiresChanged();
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

void ReceiveSwapViewModel::onNewAddressFailed()
{
    emit newAddressFailed();
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
    LOG_INFO() << "setAddressComment " << value.toStdString();
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
void ReceiveSwapViewModel::saveAddress()
{
    using namespace beam::wallet;

    if (getCommentValid()) {
        _receiverAddress.m_label = _addressComment.toStdString();
        _receiverAddress.m_duration = _offerExpires * WalletAddress::AddressExpiration1h;
        _walletModel.getAsync()->saveAddress(_receiverAddress, true);
    }
}
