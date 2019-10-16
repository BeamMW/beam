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
#include "qml_globals.h"
#include <QLocale>

SendViewModel::SendViewModel()
    : _feeGrothes(0)
    , _sendAmount(0.0)
    , _change(0)
    , _walletModel(*AppModel::getInstance().getWallet())
{
    connect(&_walletModel, &WalletModel::changeCalculated, this, &SendViewModel::onChangeCalculated);
    connect(&_walletModel, SIGNAL(sendMoneyVerified()), this, SIGNAL(sendMoneyVerified()));
    connect(&_walletModel, SIGNAL(cantSendToExpired()), this, SIGNAL(cantSendToExpired()));
    connect(&_walletModel, SIGNAL(availableChanged()), this, SIGNAL(availableChanged()));

    _walletModel.getAsync()->getWalletStatus();
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
        emit canSendChanged();
    }
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
        emit canSendChanged();
    }
}

QString SendViewModel::getReceiverTA() const
{
    return _receiverTA;
}

void SendViewModel::setReceiverTA(const QString& value)
{
    if (_receiverTA != value)
    {
       _receiverTA = value;
        emit receiverTAChanged();
        emit canSendChanged();

        if (QMLGlobals::isSwapToken(value))
        {
            // Just ignore, UI would handle this case
            // and automatically switch to another view
        }
        else
        {
            if(getRreceiverTAValid())
            {
                extractParameters();
            }
            else
            {
                // Just ignore, UI will display error automatically
            }
        }
    }
}

bool SendViewModel::getRreceiverTAValid() const
{
    return QMLGlobals::isTAValid(_receiverTA);
}

QString SendViewModel::getReceiverAddress() const
{
    if (QMLGlobals::isTransactionToken(_receiverTA))
    {
        // TODO:SWAP return extracted address if we have token.
        // Now we return token, just for tests
        return _receiverTA;
    }

    return _receiverTA;
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

double SendViewModel::getAvailable() const
{
    return beamui::Beam2Coins(_walletModel.getAvailable() - calcTotalAmount() - _change);
}

double SendViewModel::getMissing() const
{
    return beamui::Beam2Coins(calcTotalAmount() - _walletModel.getAvailable());
}

bool SendViewModel::isEnough() const
{
    return _walletModel.getAvailable() >= calcTotalAmount() + _change;
}

void SendViewModel::onChangeCalculated(beam::Amount change)
{
    _change = change;
    emit availableChanged();
    emit canSendChanged();
}

double SendViewModel::getChange() const
{
    return beamui::Beam2Coins(_change);
}

double SendViewModel::getTotalUTXO() const
{
    return beamui::Beam2Coins(calcTotalAmount() + _change);
}

bool SendViewModel::canSend() const
{
    return !QMLGlobals::isSwapToken(_receiverTA) && getRreceiverTAValid()
           && _sendAmount > 0 && isEnough()
           && QMLGlobals::isFeeOK(_feeGrothes, Currency::CurrBeam);
}

void SendViewModel::sendMoney()
{
    assert(canSend());
    if(canSend())
    {
        // TODO:SWAP show 'operation in process' animation here?
        auto messageString = _comment.toStdString();

        auto p = beam::wallet::CreateSimpleTransactionParameters()
            .SetParameter(beam::wallet::TxParameterID::PeerID, *_txParameters.GetParameter<beam::wallet::WalletID>(beam::wallet::TxParameterID::PeerID))
            .SetParameter(beam::wallet::TxParameterID::Amount, calcSendAmount())
            .SetParameter(beam::wallet::TxParameterID::Fee, calcFeeAmount())
            .SetParameter(beam::wallet::TxParameterID::Message, beam::ByteBuffer(messageString.begin(), messageString.end()));

        _walletModel.getAsync()->startTransaction(std::move(p));
    }
}

void SendViewModel::extractParameters()
{
    auto txParameters = beam::wallet::ParseParameters(_receiverTA.toStdString());
    if (!txParameters)
    {
        return;
    }

    _txParameters = *txParameters;
    if (auto amount = _txParameters.GetParameter<beam::Amount>(beam::wallet::TxParameterID::Amount); amount)
    {
        setSendAmount(static_cast<double>(*amount) / beam::Rules::Coin);
    }
    if (auto fee = _txParameters.GetParameter<beam::Amount>(beam::wallet::TxParameterID::Fee); fee)
    {
        setFeeGrothes(static_cast<int>(*fee));
    }
    if (auto comment = _txParameters.GetParameter(beam::wallet::TxParameterID::Message); comment)
    {
        std::string s(comment->begin(), comment->end());
        setComment(QString::fromStdString(s));
    }
}
