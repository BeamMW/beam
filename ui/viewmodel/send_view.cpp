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
#include "send_view.h"
#include "model/app_model.h"
#include "wallet/common.h"
#include "ui_helpers.h"
#include <QLocale>

namespace
{
    const int kDefaultFeeInGroth = 10;
    const int kFeeInGroth_Fork1 = 100;
}

SendViewModel::SendViewModel()
    : _feeGrothes(0)
    , _sendAmount(0.0)
    , _change(0)
    , _walletModel(*AppModel::getInstance().getWallet())
{
    connect(&_walletModel, &WalletModel::changeCalculated,       this,  &SendViewModel::onChangeCalculated);
    connect(&_walletModel, &WalletModel::changeCurrentWalletIDs, this,  &SendViewModel::onChangeWalletIDs);
    connect(&_walletModel, &WalletModel::sendMoneyVerified,      this,  &SendViewModel::onSendMoneyVerified);
    connect(&_walletModel, &WalletModel::cantSendToExpired,      this,  &SendViewModel::onCantSendToExpired);

    _status.setOnChanged([this]() {
        emit availableChanged();
    });

    _status.refresh();
}

int SendViewModel::getFeeGrothes() const
{
    return _feeGrothes;
}

void SendViewModel::setFeeGrothes(int value)
{
    if (value != _feeGrothes)
    {
        _feeGrothes = value;
        _walletModel.getAsync()->calcChange(calcTotalAmount());
        emit feeGrothesChanged();
    }
}

beam::Amount SendViewModel::calcFeeAmount() const
{
    return static_cast<beam::Amount>(_feeGrothes);
}

beam::Amount SendViewModel::calcSendAmount() const
{
    return std::round(_sendAmount * beam::Rules::Coin);
}

beam::Amount SendViewModel::calcTotalAmount() const
{
    return calcSendAmount() + calcFeeAmount();
}

QString SendViewModel::getComment() const
{
    return _comment;
}

void SendViewModel::setComment(const QString& value)
{
    if (_comment != value)
    {
        _comment = value;
        emit commentChanged();
    }
}

double SendViewModel::getSendAmount() const
{
    return _sendAmount;
}

void SendViewModel::setSendAmount(double value)
{
    if (value != _sendAmount)
    {
        _sendAmount = value;
        _walletModel.getAsync()->calcChange(calcTotalAmount());
        emit sendAmountChanged();
    }
}

QString SendViewModel::getReceiverAddress() const
{
    return _receiverAddr;
}

void SendViewModel::setReceiverAddress(const QString& value)
{
    if (_receiverAddr != value)
    {
        _receiverAddr = value;
        emit receiverAddressChanged();
    }
}

bool SendViewModel::isValidReceiverAddress(const QString& value)
{
    return beam::wallet::check_receiver_address(value.toStdString());
}

int SendViewModel::getDefaultFeeInGroth() const
{
    return _walletModel.isFork1() ? kFeeInGroth_Fork1 : kDefaultFeeInGroth;
}

int SendViewModel::getMinFeeInGroth() const
{
    return _walletModel.isFork1() ? kFeeInGroth_Fork1 : 0;
}

QString SendViewModel::getAvailable() const
{
    return beamui::BeamToString(_status.getAvailable() - calcTotalAmount() - _change);
}

QString SendViewModel::getMissing() const
{
    beam::Amount missed = calcTotalAmount() - _status.getAvailable();
    if (missed > 99999)
    {
        //% "BEAM"
        return beamui::BeamToString(missed) + " " + qtTrId("tx-curency-name");
    }
    //% "GROTH"
    return QLocale().toString(static_cast<qulonglong>(missed)) + " " + qtTrId("tx-curency-sub-name");
}

bool SendViewModel::isEnough() const
{
    return _status.getAvailable() >= calcTotalAmount() + _change;
}

bool SendViewModel::needPassword() const
{
    return AppModel::getInstance().getSettings().isPasswordReqiredToSpendMoney();
}

bool SendViewModel::isPasswordValid(const QString& value) const
{
    beam::SecString secretPass = value.toStdString();
    return AppModel::getInstance().checkWalletPassword(secretPass);
}

void SendViewModel::onChangeCalculated(beam::Amount change)
{
    _change = change;
    emit availableChanged();
}

QString SendViewModel::getChange() const
{
    return beamui::BeamToString(_change);
}

void SendViewModel::onChangeWalletIDs(beam::wallet::WalletID senderID, beam::wallet::WalletID receiverID)
{
    setReceiverAddress(beamui::toString(receiverID));
}

void SendViewModel::sendMoney()
{
    assert(isValidReceiverAddress(getReceiverAddress()));
    if(isValidReceiverAddress(getReceiverAddress()))
    {
        beam::wallet::WalletID walletID(beam::Zero);
        walletID.FromHex(getReceiverAddress().toStdString());

        // TODO: show 'operation in process' animation here?
        _walletModel.getAsync()->sendMoney(walletID, _comment.toStdString(), calcSendAmount(), calcFeeAmount());
    }
}

void SendViewModel::onSendMoneyVerified()
{
    // forward to qml
    emit sendMoneyVerified();
}

void SendViewModel::onCantSendToExpired()
{
    // forward to qml
    emit cantSendToExpired();
}
