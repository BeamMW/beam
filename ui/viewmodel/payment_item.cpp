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
#include "payment_item.h"
#include "ui_helpers.h"
#include "wallet/wallet.h"
#include "model/app_model.h"

using namespace beam;
using namespace beam::wallet;
using namespace beamui;

PaymentInfoItem::PaymentInfoItem(QObject* parent /*= nullptr*/)
        : QObject(parent)
{
}

QString PaymentInfoItem::getSender() const
{
    return toString(m_paymentInfo.m_Sender);
}

QString PaymentInfoItem::getReceiver() const
{
    return toString(m_paymentInfo.m_Receiver);
}

QString PaymentInfoItem::getAmount() const
{
    return AmountToUIString(m_paymentInfo.m_Amount, Currencies::Beam);
}

QString PaymentInfoItem::getAmountValue() const
{
    return AmountToUIString(m_paymentInfo.m_Amount, Currencies::Unknown);
}

QString PaymentInfoItem::getKernelID() const
{
    return toString(m_paymentInfo.m_KernelID);
}

bool PaymentInfoItem::isValid() const
{
    return m_paymentInfo.IsValid();
}

QString PaymentInfoItem::getPaymentProof() const
{
    return m_paymentProof;
}

void PaymentInfoItem::setPaymentProof(const QString& value)
{
    if (m_paymentProof != value)
    {
        m_paymentProof = value;
        try
        {
            m_paymentInfo = beam::wallet::storage::PaymentInfo::FromByteBuffer(beam::from_hex(m_paymentProof.toStdString()));
            emit paymentProofChanged();
        }
        catch (...)
        {
            reset();
        }
    }
}

void PaymentInfoItem::reset()
{
    m_paymentInfo.Reset();
    emit paymentProofChanged();
}


MyPaymentInfoItem::MyPaymentInfoItem(const TxID& txID, QObject* parent/* = nullptr*/)
        : PaymentInfoItem(parent)
{
    auto model = AppModel::getInstance().getWallet();
    connect(model.get(), SIGNAL(paymentProofExported(const beam::wallet::TxID&, const QString&)), SLOT(onPaymentProofExported(const beam::wallet::TxID&, const QString&)));
    model->getAsync()->exportPaymentProof(txID);
}

void MyPaymentInfoItem::onPaymentProofExported(const beam::wallet::TxID& txID, const QString& proof)
{
    setPaymentProof(proof);
}
