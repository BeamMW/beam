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
#include "wallet/swaps/swap_transaction.h"
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
    , _txParameters(beam::wallet::CreateSwapParameters()
        .SetParameter(beam::wallet::TxParameterID::AtomicSwapCoin, beam::wallet::AtomicSwapCoin::Bitcoin)
        .SetParameter(beam::wallet::TxParameterID::AtomicSwapIsBeamSide, true)
        .SetParameter(beam::wallet::TxParameterID::AtomicSwapSecondSideChainType, beam::wallet::SwapSecondSideChainType::Testnet)
        .SetParameter(beam::wallet::TxParameterID::IsInitiator, true))
{
    LOG_INFO() << "ReceiveSwapViewModel created";
    connect(&_walletModel, &WalletModel::generatedNewAddress, this, &ReceiveSwapViewModel::onGeneratedNewAddress);
    connect(&_walletModel, &WalletModel::newAddressFailed, this,  &ReceiveSwapViewModel::onNewAddressFailed);
    generateNewAddress();

    updateTransactionToken();
}

ReceiveSwapViewModel::~ReceiveSwapViewModel()
{
    LOG_INFO() << "ReceiveSwapViewModel destroyed";
}

void ReceiveSwapViewModel::onGeneratedNewAddress(const beam::wallet::WalletAddress& addr)
{
    _receiverAddress = addr;
    emit receiverAddressChanged();
    updateTransactionToken();
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
        updateTransactionToken();
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
        updateTransactionToken();
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
        updateTransactionToken();
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
        updateTransactionToken();
    }
}

void ReceiveSwapViewModel::setReceiveFee(int value)
{
    LOG_INFO() << "setReceiveFee " << value;
    if (value != _receiveFee)
    {
        _receiveFee = value;
        emit receiveFeeChanged();
        updateTransactionToken();
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

void ReceiveSwapViewModel::setTranasctionToken(const QString& value)
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

void ReceiveSwapViewModel::startListen()
{
    auto fee = (_sentCurrency == Currency::CurrBEAM) ? _sentFee : _receiveFee;
    auto txParameters = beam::wallet::TxParameters(_txParameters);

    txParameters.SetParameter(beam::wallet::TxParameterID::IsInitiator, !*_txParameters.GetParameter<bool>(beam::wallet::TxParameterID::IsInitiator));
    txParameters.DeleteParameter(beam::wallet::TxParameterID::PeerID);
    txParameters.SetParameter(beam::wallet::TxParameterID::MyID, *_txParameters.GetParameter<beam::wallet::WalletID>(beam::wallet::TxParameterID::PeerID));
    txParameters.SetParameter(beam::wallet::TxParameterID::Fee, beam::Amount(fee));
    txParameters.SetParameter(beam::wallet::TxParameterID::AtomicSwapIsBeamSide, !*_txParameters.GetParameter<bool>(beam::wallet::TxParameterID::AtomicSwapIsBeamSide));

    _walletModel.getAsync()->startTransaction(std::move(txParameters));
}

namespace
{
    beam::wallet::AtomicSwapCoin convertCurrencyToSwapCoin(Currency currency)
    {
        switch (currency)
        {
        case Currency::CurrBTC:
            return beam::wallet::AtomicSwapCoin::Bitcoin;
        case Currency::CurrLTC:
            return beam::wallet::AtomicSwapCoin::Litecoin;
        case Currency::CurrQTUM:
            return beam::wallet::AtomicSwapCoin::Qtum;
        default:
            return beam::wallet::AtomicSwapCoin::Unknown;
        }
    }
}

void ReceiveSwapViewModel::updateTransactionToken()
{
    // TODO:
    // _txParameters.SetParameter(beam::wallet::TxParameterID::PeerResponseHeight, ResponseTime(_offerExpires));

    if (_sentCurrency == Currency::CurrBEAM)
    {
        _txParameters.SetParameter(beam::wallet::TxParameterID::AtomicSwapIsBeamSide, false);
        _txParameters.SetParameter(beam::wallet::TxParameterID::AtomicSwapAmount, static_cast<beam::Amount>(std::round(_amountToReceive * 100000000)));// TODO: us libbitcoin::satoshi_per_bitcoin)));
        _txParameters.SetParameter(beam::wallet::TxParameterID::Amount, static_cast<beam::Amount>(std::round(_amountSent * beam::Rules::Coin)));
        _txParameters.SetParameter(beam::wallet::TxParameterID::AtomicSwapCoin, convertCurrencyToSwapCoin(_receiveCurrency));
    }
    else if (_receiveCurrency == Currency::CurrBEAM)
    {
        _txParameters.SetParameter(beam::wallet::TxParameterID::AtomicSwapIsBeamSide, true);
        _txParameters.SetParameter(beam::wallet::TxParameterID::AtomicSwapAmount, static_cast<beam::Amount>(std::round(_amountSent * 100000000)));// TODO: us libbitcoin::satoshi_per_bitcoin)));
        _txParameters.SetParameter(beam::wallet::TxParameterID::Amount, static_cast<beam::Amount>(std::round(_amountToReceive * beam::Rules::Coin)));
        _txParameters.SetParameter(beam::wallet::TxParameterID::AtomicSwapCoin, convertCurrencyToSwapCoin(_sentCurrency));
    }
    _txParameters.SetParameter(beam::wallet::TxParameterID::PeerID, _receiverAddress.m_walletID);

    setTranasctionToken(QString::fromStdString(std::to_string(_txParameters)));
}
