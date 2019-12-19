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

#include "receive_view.h"
#include "ui_helpers.h"
#include "model/qr.h"
#include "model/app_model.h"
#include <QClipboard>

namespace {
    enum {
        AddressExpires = 0,
        AddressNotExpires = 1
    };
}

ReceiveViewModel::ReceiveViewModel()
    : _amountToReceiveGrothes(0)
    , _addressExpires(AddressExpires)
    , _qr(std::make_unique<QR>())
    , _walletModel(*AppModel::getInstance().getWallet())
{
    connect(_qr.get(), &QR::qrDataChanged, this, &ReceiveViewModel::onReceiverQRChanged);
    connect(&_walletModel, &WalletModel::generatedNewAddress, this, &ReceiveViewModel::onGeneratedNewAddress);
    connect(&_walletModel, &WalletModel::newAddressFailed, this, &ReceiveViewModel::newAddressFailed);
    generateNewAddress();
    updateTransactionToken();
}

ReceiveViewModel::~ReceiveViewModel()
{
    disconnect(_qr.get(), &QR::qrDataChanged, this, &ReceiveViewModel::onReceiverQRChanged);
}

void ReceiveViewModel::onGeneratedNewAddress(const beam::wallet::WalletAddress& addr)
{
    _receiverAddress = addr;
    emit receiverAddressChanged();

    _qr->setAddr(beamui::toString(_receiverAddress.m_walletID));
    updateTransactionToken();
}

QString ReceiveViewModel::getAmountToReceive() const
{
    return beamui::AmountToUIString(_amountToReceiveGrothes);
}

void ReceiveViewModel::setAmountToReceive(QString value)
{
    auto amount = beamui::UIStringToAmount(value);
    if (amount != _amountToReceiveGrothes)
    {
        _amountToReceiveGrothes = amount;
        _qr->setAmount(_amountToReceiveGrothes);
        emit amountReceiveChanged();
        updateTransactionToken();
    }
}

void ReceiveViewModel::setAddressExpires(int value)
{
    if (value != _addressExpires)
    {
        _addressExpires = value;
        emit addressExpiresChanged();
    }
}

int ReceiveViewModel::getAddressExpires() const
{
    return _addressExpires;
}

QString ReceiveViewModel::getReceiverAddress() const
{
    return beamui::toString(_receiverAddress.m_walletID);
}

QString ReceiveViewModel::getReceiverAddressQR() const
{
    return _qr->getEncoded();
}

void ReceiveViewModel::onReceiverQRChanged()
{
    emit receiverAddressChanged();
}

void ReceiveViewModel::generateNewAddress()
{
    _receiverAddress = {};
    emit receiverAddressChanged();

    setAddressComment("");
    _walletModel.getAsync()->generateNewAddress();
}

QString ReceiveViewModel::getAddressComment() const
{
    return _addressComment;
}

void ReceiveViewModel::setTranasctionToken(const QString& value)
{
    if (_token != value)
    {
        _token = value;
        emit transactionTokenChanged();
    }
}

QString ReceiveViewModel::getTransactionToken() const
{
    return _token;
}

bool ReceiveViewModel::getCommentValid() const
{
    return !_walletModel.isAddressWithCommentExist(_addressComment.toStdString());
}

void ReceiveViewModel::setAddressComment(const QString& value)
{
    auto trimmed = value.trimmed();
    if (_addressComment != trimmed)
    {
        _addressComment = trimmed;
        emit addressCommentChanged();
        emit commentValidChanged();
    }
}

void ReceiveViewModel::saveAddress()
{
    using namespace beam::wallet;

    if (getCommentValid()) {
        _receiverAddress.m_label = _addressComment.toStdString();
        _receiverAddress.m_duration = _addressExpires == AddressExpires ? WalletAddress::AddressExpiration24h : WalletAddress::AddressExpirationNever;
        _walletModel.getAsync()->saveAddress(_receiverAddress, true);
    }
}

void ReceiveViewModel::updateTransactionToken()
{
    _txParameters.SetParameter(beam::wallet::TxParameterID::Amount, _amountToReceiveGrothes);
    _txParameters.SetParameter(beam::wallet::TxParameterID::PeerID, _receiverAddress.m_walletID);
    _txParameters.SetParameter(beam::wallet::TxParameterID::TransactionType, beam::wallet::TxType::Simple);
    setTranasctionToken(QString::fromStdString(std::to_string(_txParameters)));
}
