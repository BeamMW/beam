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
#include <QApplication>

ReceiveViewModel::ReceiveViewModel()
    : _amountToReceive(0.0)
    , _addressExpires(true)
    , _qr(std::make_unique<QR>())
    , _walletModel(*AppModel::getInstance().getWallet())
{
    LOG_INFO() << "ReceiveViewModel created";
    connect(_qr.get(), &QR::qrDataChanged, this, &ReceiveViewModel::onReceiverQRChanged);
    connect(&_walletModel, &WalletModel::generatedNewAddress, this, &ReceiveViewModel::onGeneratedNewAddress);
    connect(&_walletModel, &WalletModel::newAddressFailed, this,  &ReceiveViewModel::onNewAddressFailed);
    generateNewAddress();
}

ReceiveViewModel::~ReceiveViewModel()
{
    disconnect(_qr.get(), &QR::qrDataChanged, this, &ReceiveViewModel::onReceiverQRChanged);
    saveAddress();
    LOG_INFO() << "ReceiveViewModel destroyed";
}

void ReceiveViewModel::onGeneratedNewAddress(const beam::wallet::WalletAddress& addr)
{
    _receiverAddress = addr;
    setAddressExpires(true);
    _qr->setAddr(beamui::toString(_receiverAddress.m_walletID));
}

double ReceiveViewModel::getAmountToReceive() const
{
    return _amountToReceive;
}

void ReceiveViewModel::setAmountToReceive(double value)
{
    if (value != _amountToReceive)
    {
        _amountToReceive = value;
        _qr->setAmount(_amountToReceive);
        emit amountToReceiveChanged();
    }
}

void ReceiveViewModel::setAddressExpires(bool value)
{
    if (value != _addressExpires)
    {
        _addressExpires = value;
        emit addressExpiresChanged();
    }
}

bool ReceiveViewModel::getAddressExpires() const
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
    _addressComment = "";
    _walletModel.getAsync()->generateNewAddress();
}

void ReceiveViewModel::onNewAddressFailed()
{
    emit newAddressFailed();
}

QString ReceiveViewModel::getAddressComment() const
{
    return _addressComment;
}

void ReceiveViewModel::setAddressComment(const QString& value)
{
    auto trimmed = value.trimmed();
    if (_addressComment != trimmed)
    {
        _addressComment = trimmed;
        emit addressCommentChanged();
    }
}

bool ReceiveViewModel::isValidComment(const QString &comment) const
{
    return !_walletModel.isAddressWithCommentExist(comment.toStdString());
}

void ReceiveViewModel::saveAddress()
{
    using namespace beam::wallet;

    _receiverAddress.m_label = _addressComment.toStdString();
    _receiverAddress.m_duration = _addressExpires ? WalletAddress::AddressExpiration24h : WalletAddress::AddressExpirationNever;
    _walletModel.getAsync()->saveAddress(_receiverAddress, true);
}
