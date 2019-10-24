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
    , _sendAmountGrothes(0)
    , _changeGrothes(0)
    , _walletModel(*AppModel::getInstance().getWallet())
{
    connect(&_walletModel, &WalletModel::changeCalculated, this, &SendViewModel::onChangeCalculated);
    connect(&_walletModel, SIGNAL(sendMoneyVerified()), this, SIGNAL(sendMoneyVerified()));
    connect(&_walletModel, SIGNAL(cantSendToExpired()), this, SIGNAL(cantSendToExpired()));
    connect(&_walletModel, SIGNAL(availableChanged()), this, SIGNAL(availableChanged()));

    _walletModel.getAsync()->getWalletStatus();
}

unsigned int SendViewModel::getFeeGrothes() const
{
    return _feeGrothes;
}

void SendViewModel::setFeeGrothes(unsigned int value)
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

QString SendViewModel::getSendAmount() const
{
    LOG_INFO() << "ret Send amount grothes: " << _sendAmountGrothes << " 2ui: " << beamui::AmountToString(_sendAmountGrothes).toStdString();
    return beamui::AmountToString(_sendAmountGrothes);
}

void SendViewModel::setSendAmount(QString value)
{
    beam::Amount amount = beamui::StringToAmount(value);
    if (amount != _sendAmountGrothes)
    {
        _sendAmountGrothes = amount;
        LOG_INFO() << "Send amount: " << _sendAmountGrothes << " Coins: " << (long double)_sendAmountGrothes / beam::Rules::Coin;
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

beam::Amount SendViewModel::calcTotalAmount() const
{
    return _sendAmountGrothes + _feeGrothes;
}

QString SendViewModel::getAvailable() const
{
    return  beamui::AmountToString(isEnough() ? _walletModel.getAvailable() - calcTotalAmount() - _changeGrothes : 0);
}

QString SendViewModel::getMissing() const
{
    return beamui::AmountToString(calcTotalAmount() - _walletModel.getAvailable());
}

bool SendViewModel::isEnough() const
{
    return _walletModel.getAvailable() >= calcTotalAmount() + _changeGrothes;
}

void SendViewModel::onChangeCalculated(beam::Amount change)
{
    _changeGrothes = change;
    emit availableChanged();
    emit canSendChanged();
}

QString SendViewModel::getChange() const
{
    return beamui::AmountToString(_changeGrothes);
}

QString SendViewModel::getTotalUTXO() const
{
    return beamui::AmountToString(calcTotalAmount() + _changeGrothes);
}

bool SendViewModel::canSend() const
{
    return !QMLGlobals::isSwapToken(_receiverTA) && getRreceiverTAValid()
           && _sendAmountGrothes > 0 && isEnough()
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
            .SetParameter(beam::wallet::TxParameterID::PeerID,  *_txParameters.GetParameter<beam::wallet::WalletID>(beam::wallet::TxParameterID::PeerID))
            .SetParameter(beam::wallet::TxParameterID::Amount,  _sendAmountGrothes)
            .SetParameter(beam::wallet::TxParameterID::Fee,     _feeGrothes)
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
        setSendAmount(beamui::AmountToString(*amount));
    }
    if (auto fee = _txParameters.GetParameter<beam::Amount>(beam::wallet::TxParameterID::Fee); fee)
    {
        setFeeGrothes(*fee);
    }
    if (auto comment = _txParameters.GetParameter(beam::wallet::TxParameterID::Message); comment)
    {
        std::string s(comment->begin(), comment->end());
        setComment(QString::fromStdString(s));
    }
}
