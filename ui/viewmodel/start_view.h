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

#include <functional>

#include <QObject>
#include <QDateTime>
#include <QQmlListProperty>
#include <QTimer>
#include <QThread>

#include "wallet/wallet_db.h"
#include "mnemonic/mnemonic.h"

#include "messages_view.h"

namespace beam
{
    class HWWallet;
}

class RecoveryPhraseItem : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isCorrect READ isCorrect NOTIFY isCorrectChanged)
    Q_PROPERTY(bool isAllowed READ isAllowed NOTIFY isAllowedChanged)
    Q_PROPERTY(QString value READ getValue WRITE setValue NOTIFY valueChanged)
    Q_PROPERTY(QString phrase READ getPhrase CONSTANT)
    Q_PROPERTY(int index READ getIndex CONSTANT)
public:
    RecoveryPhraseItem(int index, const QString& phrase);
    ~RecoveryPhraseItem();

    bool isCorrect() const;
    bool isAllowed() const;
    const QString& getValue() const;
    void setValue(const QString& value);
    const QString& getPhrase() const;
    int getIndex() const;
signals: 
    void isCorrectChanged();
    void isAllowedChanged();
    void valueChanged();

private:
    int m_index;
    QString m_phrase;
    QString m_userInput;
};

class WalletDBPathItem : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString shortPath READ getShortPath CONSTANT)
    Q_PROPERTY(QString fullPath READ getFullPath CONSTANT)
    Q_PROPERTY(int fileSize READ getFileSize CONSTANT)
    Q_PROPERTY(QString lastWriteDateString READ getLastWriteDateString CONSTANT)
    Q_PROPERTY(QString creationDateString READ getCreationDateString CONSTANT)
    Q_PROPERTY(bool isPreferred READ isPreferred CONSTANT)
public:
    WalletDBPathItem(
            const QString& walletDBPath,
            uintmax_t fileSize,
            QDateTime lastWriteTime,
            QDateTime creationTime,
            bool defaultLocated = false);
    WalletDBPathItem() = default;
    virtual ~WalletDBPathItem();

    const QString& getFullPath() const;
    QString getShortPath() const;
    int getFileSize() const;
    QString getLastWriteDateString() const;
    QString getCreationDateString() const;
    QDateTime getLastWriteDate() const;
    bool locatedByDefault() const;
    void setPreferred(bool isPreferred = true);
    bool isPreferred() const;

private:
    QString m_fullPath;
    uintmax_t m_fileSize = 0;
    QDateTime m_lastWriteTime;
    QDateTime m_creationTime;
    bool m_defaultLocated = false;
    bool m_isPreferred = false;
};

#if defined(BEAM_HW_WALLET)

class TrezorThread : public QThread
{
    Q_OBJECT
public:
    TrezorThread(std::shared_ptr<beam::HWWallet> hw);

    void run() override;

signals:
    void ownerKeyImported(const QString& key);

private:
    std::shared_ptr<beam::HWWallet> m_hw;
};

#endif

class StartViewModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool walletExists READ walletExists NOTIFY walletExistsChanged)
    Q_PROPERTY(bool isRecoveryMode READ getIsRecoveryMode WRITE setIsRecoveryMode NOTIFY isRecoveryModeChanged)
    Q_PROPERTY(QList<QObject*> recoveryPhrases READ getRecoveryPhrases NOTIFY recoveryPhrasesChanged)
    Q_PROPERTY(QList<QObject*> checkPhrases READ getCheckPhrases NOTIFY checkPhrasesChanged)
    Q_PROPERTY(QChar phrasesSeparator READ getPhrasesSeparator CONSTANT)
    Q_PROPERTY(bool isTrezorEnabled READ isTrezorEnabled CONSTANT)

#if defined(BEAM_HW_WALLET)
    Q_PROPERTY(bool isTrezorConnected READ isTrezorConnected NOTIFY isTrezorConnectedChanged)
    Q_PROPERTY(QString trezorDeviceName READ getTrezorDeviceName NOTIFY trezorDeviceNameChanged)
    Q_PROPERTY(bool isOwnerKeyImported READ isOwnerKeyImported NOTIFY isOwnerKeyImportedChanged)
#endif

    Q_PROPERTY(int localPort READ getLocalPort CONSTANT)
    Q_PROPERTY(QString remoteNodeAddress READ getRemoteNodeAddress CONSTANT)
    Q_PROPERTY(QString localNodePeer READ getLocalNodePeer CONSTANT)
    Q_PROPERTY(QQmlListProperty<WalletDBPathItem> walletDBpaths READ getWalletDBpaths CONSTANT)
    Q_PROPERTY(bool isCapsLockOn READ isCapsLockOn NOTIFY capsLockStateMayBeChanged)
    Q_PROPERTY(bool validateDictionary READ getValidateDictionary WRITE setValidateDictionary NOTIFY validateDictionaryChanged)

public:

    using DoneCallback = std::function<bool (beam::wallet::IWalletDB::Ptr db, const std::string& walletPass)>;

    StartViewModel();
    ~StartViewModel();

    bool walletExists() const;
    bool isTrezorEnabled() const;

#if defined(BEAM_HW_WALLET)
    bool isTrezorConnected() const;
    QString getTrezorDeviceName() const;
    bool isOwnerKeyImported() const;
#endif

    bool getIsRecoveryMode() const;
    void setIsRecoveryMode(bool value);
    const QList<QObject*>& getRecoveryPhrases();
    const QList<QObject*>& getCheckPhrases();
    QChar getPhrasesSeparator();
    int getLocalPort() const;
    QString getRemoteNodeAddress() const;
    QString getLocalNodePeer() const;
    QQmlListProperty<WalletDBPathItem> getWalletDBpaths();
    bool isCapsLockOn() const;
    bool getValidateDictionary() const;
    void setValidateDictionary(bool value);

    Q_INVOKABLE void setupLocalNode(int port, const QString& localNodePeer);
    Q_INVOKABLE void setupRemoteNode(const QString& nodeAddress);
    Q_INVOKABLE void setupRandomNode();
    Q_INVOKABLE uint coreAmount() const;
    Q_INVOKABLE void copyPhrasesToClipboard();
    Q_INVOKABLE void printRecoveryPhrases(QVariant viewData);
    Q_INVOKABLE void resetPhrases();
    Q_INVOKABLE bool getIsRunLocalNode() const;
    Q_INVOKABLE QString chooseRandomNode() const;
    Q_INVOKABLE QString walletVersion() const;
    Q_INVOKABLE bool isFindExistingWalletDB();
    Q_INVOKABLE void deleteCurrentWalletDB();
    Q_INVOKABLE void migrateWalletDB(const QString& path);
    Q_INVOKABLE QString selectCustomWalletDB();
    Q_INVOKABLE QString defaultPortToListen() const;
    Q_INVOKABLE QString defaultRemoteNodeAddr() const;
    Q_INVOKABLE void checkCapsLock();
    Q_INVOKABLE void openFolder(const QString& path) const;

#if defined(BEAM_HW_WALLET)
    Q_INVOKABLE void startOwnerKeyImporting();
    Q_INVOKABLE bool isPasswordValid(const QString& pass);
    Q_INVOKABLE void setOwnerKeyPassword(const QString& pass);
#endif

signals:
    void walletExistsChanged();
    void generateGenesysyBlockChanged();
    void recoveryPhrasesChanged();
    void checkPhrasesChanged();
    void isRecoveryModeChanged();
    void capsLockStateMayBeChanged();
    void validateDictionaryChanged();

#if defined(BEAM_HW_WALLET)
    void isTrezorConnectedChanged();
    void trezorDeviceNameChanged();
    void isOwnerKeyImportedChanged();
#endif

public slots:
    bool createWallet();
    bool openWallet(const QString& pass);
    bool checkWalletPassword(const QString& password) const;
    void setPassword(const QString& pass);
    void onNodeSettingsChanged();

#if defined(BEAM_HW_WALLET)
    void onTrezorOwnerKeyImported(const QString& key);
    void checkTrezor();
#endif

private:

    void findExistingWalletDB();

    QList<QObject*> m_recoveryPhrases;
    QList<QObject*> m_checkPhrases;
    beam::WordList m_generatedPhrases;
    std::string m_password;

    QList<WalletDBPathItem*> m_walletDBpaths;

    bool m_isRecoveryMode;
    bool m_validateDictionary = true;

#if defined(BEAM_HW_WALLET)
    std::shared_ptr<beam::HWWallet> m_hwWallet;
    QTimer m_trezorTimer;
    bool m_isTrezorConnected = false;
    TrezorThread m_trezorThread;
    std::string m_ownerKeyEncrypted;
    std::string m_ownerKeyPass;
#endif
};
