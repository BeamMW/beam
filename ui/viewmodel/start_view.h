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
#include <functional>
#include <QQmlListProperty>

#include "wallet/wallet_db.h"
#include "wallet/mnemonic.h"

#include "messages_view.h"

class RecoveryPhraseItem : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isCorrect READ isCorrect NOTIFY isCorrectChanged)
    Q_PROPERTY(QString value READ getValue WRITE setValue NOTIFY valueChanged)
    Q_PROPERTY(int index READ getIndex CONSTANT)
public:
    RecoveryPhraseItem(int index, const QString& phrase);
    ~RecoveryPhraseItem();

    bool isCorrect() const;
    const QString& getValue() const;
    void setValue(const QString& value);
    int getIndex() const;
signals: 
    void isCorrectChanged();
    void valueChanged();

private:
    int m_index;
    QString m_phrase;
    QString m_userInput;

};

class StartViewModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool walletExists READ walletExists NOTIFY walletExistsChanged)
    Q_PROPERTY(QStringList recoveryPhrases READ getRecoveryPhrases NOTIFY recoveryPhrasesChanged)
    Q_PROPERTY(QList<QObject*> checkPhrases READ getCheckPhrases NOTIFY checkPhrasesChanged)
public:

    using DoneCallback = std::function<bool (beam::IKeyChain::Ptr db, const std::string& walletPass)>;

    StartViewModel();
    ~StartViewModel();

    bool walletExists() const;
    const QStringList& getRecoveryPhrases();
    const QList<QObject*>& getCheckPhrases();

    Q_INVOKABLE void setupLocalNode(int port, int miningThreads, bool generateGenesys = false);
    Q_INVOKABLE void setupRemoteNode(const QString& nodeAddress);
    Q_INVOKABLE void setupTestnetNode();
    Q_INVOKABLE uint coreAmount() const;
    Q_INVOKABLE void copyPhrasesToClipboard();
    Q_INVOKABLE void printRecoveryPhrases(QVariant viewData);

signals:
    void walletExistsChanged();
    void generateGenesysyBlockChanged();
    void recoveryPhrasesChanged();
    void checkPhrasesChanged();

public slots:
    bool createWallet(const QString& pass);
    bool openWallet(const QString& pass);
private:
    QStringList m_recoveryPhrases;
    QList<QObject*> m_checkPhrases;
    beam::WordList m_generatedPhrases;
};
