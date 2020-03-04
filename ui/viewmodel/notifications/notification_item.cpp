// Copyright 2020 The Beam Team
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

#include "notification_item.h"
#include "utility/helpers.h"
#include "wallet/core/common.h"
#include "ui/viewmodel/ui_helpers.h"
#include "ui/viewmodel/qml_globals.h"

using namespace beam::wallet;

namespace
{
    TxParameters getTxParameters(const Notification& notification)
    {
        TxToken token;
        Deserializer d;
        d.reset(notification.m_content);
        d& token;
        return token.UnpackParameters();
    }

    Amount getAmount(const TxParameters& p)
    {
        return *p.GetParameter<Amount>(TxParameterID::Amount);
    }

    WalletID getPeerID(const TxParameters& p)
    {
        return *p.GetParameter<WalletID>(TxParameterID::PeerID);
    }

    bool isSender(const TxParameters& p)
    {
        return *p.GetParameter<bool>(TxParameterID::IsSender);
    }
}

NotificationItem::NotificationItem(const Notification& notification)
    : m_notification{notification}
{}

bool NotificationItem::operator==(const NotificationItem& other) const
{
    return getID() == other.getID();
}

ECC::uintBig NotificationItem::getID() const
{
    return m_notification.m_ID;
}

QDateTime NotificationItem::timeCreated() const
{
    QDateTime datetime;
    datetime.setTime_t(m_notification.m_createTime);
    return datetime;
}

QString NotificationItem::title() const
{
    switch(m_notification.m_type)
    {
        case Notification::Type::SoftwareUpdateAvailable:
        {
            VersionInfo info;
            if (fromByteBuffer(m_notification.m_content, info))
            {
                QString ver = QString::fromStdString(info.m_version.to_string());
                //% "New version v %1 is avalable"
                return qtTrId("notification-update-title").arg(ver);
            }
            else
            {
                LOG_ERROR() << "Software update notification deserialization error";
                return QString();
            }
        }
        case Notification::Type::AddressStatusChanged:
            //% "Address expired"
            return qtTrId("notification-address-expired");
        case Notification::Type::TransactionCompleted:
        {
            auto p = getTxParameters(m_notification);
            if (isSender(p))
            {
                //% "Transaction sent"
                return qtTrId("notification-transaction-sent");
            }
            //% "Transaction received"
            return qtTrId("notification-transaction-received");
        }            
        case Notification::Type::TransactionFailed:
            //% "Transaction failed"
            return qtTrId("notification-transaction-failed");
        case Notification::Type::BeamNews:
            //% "BEAM in the press"
            return qtTrId("notification-news");
        default:
            return "error";
    }
}

QString NotificationItem::message() const
{
    switch(m_notification.m_type)
    {
        case Notification::Type::SoftwareUpdateAvailable:
        {
            VersionInfo info;
            if (fromByteBuffer(m_notification.m_content, info))
            {
                QString currentVer = QString::fromStdString(beamui::getCurrentAppVersion().to_string());
                QString message("Your current version is v");
                message.append(currentVer);
                message.append(". Please update to get the most of your Beam wallet.");
                return message;
            }
            else
            {
                LOG_ERROR() << "Software update notification deserialization error";
                return QString();
            }
        }
        case Notification::Type::AddressStatusChanged:
            //% "Address expired"
            return qtTrId("notification-address-expired-message");
        case Notification::Type::TransactionCompleted:
        {
            auto p = getTxParameters(m_notification);
            QString message =  (isSender(p) ?
                //% "You sent <b>%1</b> BEAM to <b>%2</b>."
                qtTrId("notification-transaction-sent-message")
                :
                //% "You received <b>%1 BEAM</b> from <b>%2</b>."
                qtTrId("notification-transaction-received-message"));
            return message.arg(getAmount(p)).arg(std::to_string(getPeerID(p)).c_str());
        }
        case Notification::Type::TransactionFailed:
        {
            auto p = getTxParameters(m_notification);
            QString message = (isSender(p) ?
                //% "Sending <b>%1 BEAM</b> to <b>%2</b> failed."
                qtTrId("notification-transaction-send-failed-message")
                :
                //% "Receiving <b>%1 BEAM</b> from <b>%2</b> failed."
                qtTrId("notification-transaction-receive-failed-message"));
            return message.arg(getAmount(p)).arg(std::to_string(getPeerID(p)).c_str());
        }
        case Notification::Type::BeamNews:
            return "BEAM in the press";
        default:
            return "error";
    }
}

QString NotificationItem::type() const
{
    // !TODO: full list of the supported item types is: update expired received sent failed inpress hotnews videos events newsletter community
    
    switch(m_notification.m_type)
    {
        case Notification::Type::SoftwareUpdateAvailable:
            return "update";
        case Notification::Type::AddressStatusChanged:
            return "expired";
        case Notification::Type::TransactionCompleted:
        {
            auto p = getTxParameters(m_notification);
            return (isSender(p) ? "sent" : "received");
        }
        case Notification::Type::TransactionFailed:
            return "failed";
        case Notification::Type::BeamNews:
            return "newsletter";
        default:
            return "error";
    }
}

QString NotificationItem::state() const
{
    switch(m_notification.m_state)
    {
        case Notification::State::Unread:
            return "unread";
        case Notification::State::Read:
            return "read";
        case Notification::State::Deleted:
            return "deleted";
        default:
            return "error";
    }
}

QString NotificationItem::getTxID() const
{
    try
    {
        auto p = getTxParameters(m_notification);
        return QString::fromStdString(std::to_string(*p.GetTxID()));
    }
    catch(...)
    { }
    return "";
}
