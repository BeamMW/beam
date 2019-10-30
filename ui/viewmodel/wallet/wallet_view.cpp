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

#include "wallet_view.h"

#include <iomanip>

#include <QApplication>
#include <QtGui/qimage.h>
#include <QtCore/qbuffer.h>
#include <QUrlQuery>
#include <QClipboard>

#include "qrcode/QRCodeGenerator.h"
#include "utility/helpers.h"
#include "model/app_model.h"
#include "model/qr.h"
#include "viewmodel/ui_helpers.h"

using namespace beam;
using namespace beam::wallet;
using namespace std;
using namespace beamui;

WalletViewModel::WalletViewModel()
    : _model(*AppModel::getInstance().getWallet())
    , _settings(AppModel::getInstance().getSettings())
{
    connect(&_model, SIGNAL(transactionsChanged(beam::wallet::ChangeAction, const std::vector<beam::wallet::TxDescription>&)),
        SLOT(onTransactionsChanged(beam::wallet::ChangeAction, const std::vector<beam::wallet::TxDescription>&)));

    connect(&_model, SIGNAL(availableChanged()), this, SIGNAL(beamAvailableChanged()));
    connect(&_model, SIGNAL(receivingChanged()), this, SIGNAL(beamReceivingChanged()));
    connect(&_model, SIGNAL(sendingChanged()), this, SIGNAL(beamSendingChanged()));
    connect(&_model, SIGNAL(maturingChanged()), this, SIGNAL(beamLockedChanged()));
    connect(&_model, SIGNAL(receivingChangeChanged()), this, SIGNAL(beamReceivingChanged()));
    connect(&_model, SIGNAL(receivingIncomingChanged()), this, SIGNAL(beamReceivingChanged()));

    _model.getAsync()->getTransactions();
}

QAbstractItemModel* WalletViewModel::getTransactions()
{
    return &_transactionsList;
}

void WalletViewModel::cancelTx(QVariant variantTxID)
{
    if (!variantTxID.isNull() && variantTxID.isValid())
    {
        auto txId = variantTxID.value<beam::wallet::TxID>();
        _model.getAsync()->cancelTx(txId);
    }
}

void WalletViewModel::deleteTx(QVariant variantTxID)
{
    if (!variantTxID.isNull() && variantTxID.isValid())
    {
        auto txId = variantTxID.value<beam::wallet::TxID>();
        _model.getAsync()->deleteTx(txId);
    }
}

PaymentInfoItem* WalletViewModel::getPaymentInfo(QVariant variantTxID)
{
    if (!variantTxID.isNull() && variantTxID.isValid())
    {
        auto txId = variantTxID.value<beam::wallet::TxID>();
        return new MyPaymentInfoItem(txId, this);
    }
    else return Q_NULLPTR;
}

void WalletViewModel::onTransactionsChanged(beam::wallet::ChangeAction action, const std::vector<beam::wallet::TxDescription>& transactions)
{
    vector<shared_ptr<TxObject>> modifiedTransactions;
    modifiedTransactions.reserve(transactions.size());

    for (const auto& t : transactions)
    {
        if (t.GetParameter<TxType>(TxParameterID::TransactionType) != TxType::AtomicSwap)
        {
            modifiedTransactions.push_back(make_shared<TxObject>(t));
        }
    }

    switch (action)
    {
        case ChangeAction::Reset:
            {
                _transactionsList.reset(modifiedTransactions);
                break;
            }

        case ChangeAction::Removed:
            {
                _transactionsList.remove(modifiedTransactions);
                break;
            }

        case ChangeAction::Added:
            {
                _transactionsList.insert(modifiedTransactions);
                break;
            }
        
        case ChangeAction::Updated:
            {
                _transactionsList.update(modifiedTransactions);
                break;
            }

        default:
            assert(false && "Unexpected action");
            break;
    }

    emit transactionsChanged();
}

QString WalletViewModel::beamAvailable() const
{
    return beamui::AmountToUIString(_model.getAvailable());
}

QString WalletViewModel::beamReceiving() const
{
    // TODO:SWAP return real value
    return beamui::AmountToUIString(_model.getReceivingChange() + _model.getReceivingIncoming());
}

QString WalletViewModel::beamSending() const
{
    return beamui::AmountToUIString(_model.getSending());
}

QString WalletViewModel::beamReceivingChange() const
{
    // TODO:SWAP return real value
    return beamui::AmountToUIString(_model.getReceivingChange());
}

QString WalletViewModel::beamReceivingIncoming() const
{
    // TODO:SWAP return real value
    return beamui::AmountToUIString(_model.getReceivingIncoming());
}

QString WalletViewModel::beamLocked() const
{
    return beamLockedMaturing();
}

QString WalletViewModel::beamLockedMaturing() const
{
    return beamui::AmountToUIString(_model.getMaturing());
}

bool WalletViewModel::isAllowedBeamMWLinks() const
{
    return _settings.isAllowedBeamMWLinks();
}

void WalletViewModel::allowBeamMWLinks(bool value)
{
    _settings.setAllowedBeamMWLinks(value);
}
