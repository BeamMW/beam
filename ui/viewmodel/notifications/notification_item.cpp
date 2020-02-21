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

NotificationItem::NotificationItem(const Notification& notification)
    : m_notification{notification}
{}

bool NotificationItem::operator==(const NotificationItem& other) const
{
    return getID() == other.getID();
}

auto NotificationItem::getID() const -> ECC::uintBig
{
    return m_notification.m_ID;
}

auto NotificationItem::timeCreated() const -> QDateTime
{
    QDateTime datetime;
    datetime.setTime_t(m_notification.m_createTime);
    return datetime;
}

auto NotificationItem::title() const -> QString
{
    switch(m_notification.m_type)
    {
        case Notification::Type::SoftwareUpdateAvailable:
        {
            VersionInfo info;
            if (fromByteBuffer(m_notification.m_content, info))
            {
                QString ver = QString::fromStdString(info.m_version.to_string());
                QString title("New version v");
                title.append(ver);
                title.append(" is avalable");
                return title;
            }
            else
            {
                LOG_ERROR() << "Software update notification deserialization error";
                return QString();
            }
        }
        case Notification::Type::AddressStatusChanged:
            return "Address expired";
        case Notification::Type::TransactionStatusChanged:
            return "Transaction received";
        case Notification::Type::BeamNews:
            return "BEAM in the press";
        default:
            return "error";
    }
}

auto NotificationItem::message() const -> QString
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
            return "Address expired";
        case Notification::Type::TransactionStatusChanged:
            return "Transaction received";
        case Notification::Type::BeamNews:
            return "BEAM in the press";
        default:
            return "error";
    }
}

auto NotificationItem::type() const -> QString
{
    switch(m_notification.m_type)
    {
        case Notification::Type::SoftwareUpdateAvailable:
            return "softwareUpdate";
        case Notification::Type::AddressStatusChanged:
            return "addressChanged";
        case Notification::Type::TransactionStatusChanged:
            return "txStatusChanged";
        case Notification::Type::BeamNews:
            return "beamNews";
        default:
            return "error";
    }
}

auto NotificationItem::state() const -> QString
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
