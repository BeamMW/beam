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
#include <QSettings>
#include <QQmlListProperty>

#include "model/settings.h"
#include "wallet/bitcoin/bitcoin_client.h"
#include "wallet/bitcoin/bitcoin_settings.h"



class SettingsViewModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString nodeAddress READ getNodeAddress WRITE setNodeAddress NOTIFY nodeAddressChanged)
    Q_PROPERTY(QString version READ getVersion CONSTANT)
    Q_PROPERTY(bool localNodeRun READ getLocalNodeRun WRITE setLocalNodeRun NOTIFY localNodeRunChanged)
    Q_PROPERTY(uint localNodePort READ getLocalNodePort WRITE setLocalNodePort NOTIFY localNodePortChanged)
    Q_PROPERTY(bool isChanged READ isChanged NOTIFY propertiesChanged)
    Q_PROPERTY(QStringList localNodePeers READ getLocalNodePeers NOTIFY localNodePeersChanged)
    Q_PROPERTY(int lockTimeout READ getLockTimeout WRITE setLockTimeout NOTIFY lockTimeoutChanged)
    Q_PROPERTY(QString walletLocation READ getWalletLocation CONSTANT)
    Q_PROPERTY(bool isLocalNodeRunning READ isLocalNodeRunning NOTIFY localNodeRunningChanged)
    Q_PROPERTY(bool isPasswordReqiredToSpendMoney READ isPasswordReqiredToSpendMoney WRITE setPasswordReqiredToSpendMoney NOTIFY passwordReqiredToSpendMoneyChanged)
    Q_PROPERTY(bool isAllowedBeamMWLinks READ isAllowedBeamMWLinks WRITE allowBeamMWLinks NOTIFY beamMWLinksAllowed)
    Q_PROPERTY(QStringList supportedLanguages READ getSupportedLanguages NOTIFY currentLanguageIndexChanged)
    Q_PROPERTY(int currentLanguageIndex READ getCurrentLanguageIndex NOTIFY currentLanguageIndexChanged)
    Q_PROPERTY(QString currentLanguage READ getCurrentLanguage WRITE setCurrentLanguage)
    Q_PROPERTY(bool isValidNodeAddress READ isValidNodeAddress NOTIFY validNodeAddressChanged)

    Q_PROPERTY(QString  btcUser        READ getBTCUser         WRITE setBTCUser         NOTIFY btcUserChanged)
    Q_PROPERTY(QString  btcPass        READ getBTCPass         WRITE setBTCPass         NOTIFY btcPassChanged)
    Q_PROPERTY(QString  btcNodeAddress READ getBTCNodeAddress  WRITE setBTCNodeAddress  NOTIFY btcNodeAddressChanged)
    Q_PROPERTY(int      btcFeeRate     READ getBTCFeeRate      WRITE setBTCFeeRate      NOTIFY btcFeeRateChanged)

    Q_PROPERTY(QString  ltcUser        READ getLTCUser         WRITE setLTCUser         NOTIFY ltcUserChanged)
    Q_PROPERTY(QString  ltcPass        READ getLTCPass         WRITE setLTCPass         NOTIFY ltcPassChanged)
    Q_PROPERTY(QString  ltcNodeAddress READ getLTCNodeAddress  WRITE setLTCNodeAddress  NOTIFY ltcNodeAddressChanged)
    Q_PROPERTY(int      ltcFeeRate     READ getLTCFeeRate      WRITE setLTCFeeRate      NOTIFY ltcFeeRateChanged)

    Q_PROPERTY(QString  qtumUser        READ getQTUMUser        WRITE setQTUMUser         NOTIFY qtumUserChanged)
    Q_PROPERTY(QString  qtumPass        READ getQTUMPass        WRITE setQTUMPass         NOTIFY qtumPassChanged)
    Q_PROPERTY(QString  qtumNodeAddress READ getQTUMNodeAddress WRITE setQTUMNodeAddress  NOTIFY qtumNodeAddressChanged)
    Q_PROPERTY(int      qtumFeeRate     READ getQTUMFeeRate     WRITE setQTUMFeeRate      NOTIFY qtumFeeRateChanged)

public:

    SettingsViewModel();

    QString getNodeAddress() const;
    void setNodeAddress(const QString& value);
    QString getVersion() const;
    bool getLocalNodeRun() const;
    void setLocalNodeRun(bool value);
    uint getLocalNodePort() const;
    void setLocalNodePort(uint value);
    int getLockTimeout() const;
    void setLockTimeout(int value);
    bool isPasswordReqiredToSpendMoney() const;
    void setPasswordReqiredToSpendMoney(bool value);
    bool isAllowedBeamMWLinks() const;
    void allowBeamMWLinks(bool value);
    QStringList getSupportedLanguages() const;
    int getCurrentLanguageIndex() const;
    void setCurrentLanguageIndex(int value);
    QString getCurrentLanguage() const;
    void setCurrentLanguage(QString value);

    QStringList getLocalNodePeers() const;
    void setLocalNodePeers(const QStringList& localNodePeers);
    QString getWalletLocation() const;
    bool isLocalNodeRunning() const;
    bool isValidNodeAddress() const;

    bool isChanged() const;

    QString getBTCUser() const;
    void setBTCUser(const QString& value);
    QString getBTCPass() const;
    void setBTCPass(const QString& value);
    QString getBTCNodeAddress() const;
    void setBTCNodeAddress(const QString& value);
    int getBTCFeeRate() const;
    void setBTCFeeRate(int value);

    QString getLTCUser() const;
    void setLTCUser(const QString& value);
    QString getLTCPass() const;
    void setLTCPass(const QString& value);
    QString getLTCNodeAddress() const;
    void setLTCNodeAddress(const QString& value);
    int getLTCFeeRate() const;
    void setLTCFeeRate(int value);

    QString getQTUMUser() const;
    void setQTUMUser(const QString& value);
    QString getQTUMPass() const;
    void setQTUMPass(const QString& value);
    QString getQTUMNodeAddress() const;
    void setQTUMNodeAddress(const QString& value);
    int getQTUMFeeRate() const;
    void setQTUMFeeRate(int value);

    Q_INVOKABLE uint coreAmount() const;
    Q_INVOKABLE void addLocalNodePeer(const QString& localNodePeer);
    Q_INVOKABLE void deleteLocalNodePeer(int index);
    Q_INVOKABLE void openUrl(const QString& url);
    Q_INVOKABLE void refreshWallet();
    Q_INVOKABLE void openFolder(const QString& path);
    Q_INVOKABLE bool checkWalletPassword(const QString& password) const;
    Q_INVOKABLE void applyBtcSettings();
    Q_INVOKABLE void applyLtcSettings();
    Q_INVOKABLE void applyQtumSettings();
    //Q_INVOKABLE void disableBtc();
    //Q_INVOKABLE void disableLtc();
    //Q_INVOKABLE void disableQtum();

public slots:
    void applyChanges();
    void undoChanges();
	void reportProblem();
    void changeWalletPassword(const QString& pass);
    void onNodeStarted();
    void onNodeStopped();
    void onAddressChecked(const QString& addr, bool isValid);

signals:
    void nodeAddressChanged();
    void localNodeRunChanged();
    void localNodePortChanged();
    void localNodePeersChanged();
    void propertiesChanged();
    void lockTimeoutChanged();
    void localNodeRunningChanged();
    void passwordReqiredToSpendMoneyChanged();
    void validNodeAddressChanged();
    void currentLanguageIndexChanged();
    void beamMWLinksAllowed();

    void btcUserChanged();
    void btcPassChanged();
    void btcNodeAddressChanged();
    void btcFeeRateChanged();

    void ltcUserChanged();
    void ltcPassChanged();
    void ltcNodeAddressChanged();
    void ltcFeeRateChanged();

    void qtumUserChanged();
    void qtumPassChanged();
    void qtumNodeAddressChanged();
    void qtumFeeRateChanged();

protected:
    void timerEvent(QTimerEvent *event) override;

private:
    WalletSettings& m_settings;

    QString m_nodeAddress;
    bool m_localNodeRun;
    uint m_localNodePort;
    QStringList m_localNodePeers;
    int m_lockTimeout;
    bool m_isPasswordReqiredToSpendMoney;
    bool m_isAllowedBeamMWLinks;
    bool m_isValidNodeAddress;
    bool m_isNeedToCheckAddress;
    bool m_isNeedToApplyChanges;
    QStringList m_supportedLanguages;
    int m_currentLanguageIndex;
    int m_timerId;

    QString m_bitcoinUser;
    QString m_bitcoinPass;
    QString m_bitcoinNodeAddress;
    int m_bitcoinFeeRate = 0;

    QString m_litecoinUser;
    QString m_litecoinPass;
    QString m_litecoinNodeAddress;
    int m_litecoinFeeRate = 0;

    QString m_qtumUser;
    QString m_qtumPass;
    QString m_qtumNodeAddress;
    int m_qtumFeeRate = 0;

    const int CHECK_INTERVAL = 1000;
};
