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
#include "mnemonic/mnemonic.h"

#include "messages_view.h"

class RecoveryPhraseItem : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isCorrect READ isCorrect NOTIFY isCorrectChanged)
    Q_PROPERTY(QString value READ getValue WRITE setValue NOTIFY valueChanged)
    Q_PROPERTY(QString phrase READ getPhrase CONSTANT)
    Q_PROPERTY(int index READ getIndex CONSTANT)
public:
    RecoveryPhraseItem(int index, const QString& phrase);
    ~RecoveryPhraseItem();

    bool isCorrect() const;
    const QString& getValue() const;
    void setValue(const QString& value);
    const QString& getPhrase() const;
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
    Q_PROPERTY(bool isRecoveryMode READ getIsRecoveryMode WRITE setIsRecoveryMode NOTIFY isRecoveryModeChanged)
    Q_PROPERTY(QList<QObject*> recoveryPhrases READ getRecoveryPhrases NOTIFY recoveryPhrasesChanged)
    Q_PROPERTY(QList<QObject*> checkPhrases READ getCheckPhrases NOTIFY checkPhrasesChanged)
    Q_PROPERTY(QChar phrasesSeparator READ getPhrasesSeparator CONSTANT)
    Q_PROPERTY(bool useGpu READ getUseGpu WRITE setUseGpu NOTIFY useGpuChanged)

    Q_PROPERTY(int localPort READ getLocalPort CONSTANT)
    Q_PROPERTY(int localMiningThreads READ getLocalMiningThreads CONSTANT)
    Q_PROPERTY(QString remoteNodeAddress READ getRemoteNodeAddress CONSTANT)

public:

    using DoneCallback = std::function<bool (beam::IWalletDB::Ptr db, const std::string& walletPass)>;

    StartViewModel();
    ~StartViewModel();

    bool walletExists() const;
    bool getIsRecoveryMode() const;
    void setIsRecoveryMode(bool value);
    const QList<QObject*>& getRecoveryPhrases();
    const QList<QObject*>& getCheckPhrases();
    QChar getPhrasesSeparator();
    void setUseGpu(bool value);
    bool getUseGpu() const;
    int getLocalPort() const;
    int getLocalMiningThreads() const;
    QString getRemoteNodeAddress() const;

    Q_INVOKABLE void setupLocalNode(int port, int miningThreads);
    Q_INVOKABLE void setupRemoteNode(const QString& nodeAddress);
    Q_INVOKABLE void setupRandomNode();
    Q_INVOKABLE uint coreAmount() const;
    Q_INVOKABLE void copyPhrasesToClipboard();
    Q_INVOKABLE void printRecoveryPhrases(QVariant viewData);
    Q_INVOKABLE void resetPhrases();
    Q_INVOKABLE bool showUseGpu() const;
    Q_INVOKABLE bool hasSupportedGpu();
    Q_INVOKABLE bool getIsRunLocalNode() const;

signals:
    void walletExistsChanged();
    void generateGenesysyBlockChanged();
    void recoveryPhrasesChanged();
    void checkPhrasesChanged();
    void isRecoveryModeChanged();
    void useGpuChanged();

public slots:
    bool createWallet(const QString& pass);
    bool openWallet(const QString& pass);
private:
    QList<QObject*> m_recoveryPhrases;
    QList<QObject*> m_checkPhrases;
    beam::WordList m_generatedPhrases;
    bool m_isRecoveryMode;
};
