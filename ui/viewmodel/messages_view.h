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

#pragma once

#include <QObject>


class MessagesViewModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QStringList messages READ getMessages NOTIFY messagesChanged)

public:

    Q_INVOKABLE void deleteMessage(int index);

public:

    MessagesViewModel();

    void AddMessage(const QString& value);

    QStringList getMessages() const;
public slots:

    void onNewMessage(const QString& message);
signals:

    void messagesChanged();

private:
    QStringList m_messages;
};
