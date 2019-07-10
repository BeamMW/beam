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

#include "messages_view.h"
#include "model/app_model.h"

MessagesViewModel::MessagesViewModel()
{
    auto& model = AppModel::getInstance().getMessages();
    connect(&model, SIGNAL(newMessage(const QString&)),
        SLOT(onNewMessage(const QString&)));
}

void MessagesViewModel::deleteMessage(int index)
{
    m_messages.removeAt(index);
    emit messagesChanged();
}

void MessagesViewModel::AddMessage(const QString& value)
{
    if (m_messages.isEmpty() || !m_messages.contains(value)) {
        m_messages.push_back(value);
        emit messagesChanged();
    }
}

QStringList MessagesViewModel::getMessages() const
{
    return m_messages;
}

void MessagesViewModel::onNewMessage(const QString& message)
{
    AddMessage(message);
}
