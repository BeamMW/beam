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

#ifndef QMLTRANSLATOR_H
#define QMLTRANSLATOR_H

#include <QObject>
#include <QTranslator>

class QmlTranslator : public QObject
{
    Q_OBJECT

public:
    explicit QmlTranslator(QObject *parent = 0);

signals:
    // Сигнал об изменении текущего языка для изменения перевода интерфейса
    void languageChanged();

public:
    // Метод установки перевода, который будет доступен в QML
    Q_INVOKABLE void setTranslation(QString translation);

private:
    QTranslator m_translator;
};

#endif // QMLTRANSLATOR_H
