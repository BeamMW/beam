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
#include "wallet/bitcoin/client.h"
#include "wallet/bitcoin/settings.h"

class SwapCoinClientModel;

class SwapCoinSettingsItem : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString  feeRateLabel READ getFeeRateLabel                         CONSTANT)
    Q_PROPERTY(int      minFeeRate   READ getMinFeeRate                           CONSTANT)
    Q_PROPERTY(QString  title        READ getTitle                                NOTIFY titleChanged)
    Q_PROPERTY(int      feeRate      READ getFeeRate      WRITE setFeeRate        NOTIFY feeRateChanged)
    // node settings
    Q_PROPERTY(QString  nodeUser     READ getNodeUser     WRITE setNodeUser       NOTIFY nodeUserChanged)
    Q_PROPERTY(QString  nodePass     READ getNodePass     WRITE setNodePass       NOTIFY nodePassChanged)
    Q_PROPERTY(QString  nodeAddress  READ getNodeAddress  WRITE setNodeAddress    NOTIFY nodeAddressChanged)
    // electrum settings
    Q_PROPERTY(QString  seedElectrum        READ getSeedElectrum         WRITE setSeedElectrum         NOTIFY seedElectrumChanged)
    Q_PROPERTY(QString  nodeAddressElectrum READ getNodeAddressElectrum  WRITE setNodeAddressElectrum  NOTIFY nodeAddressElectrumChanged)

    Q_PROPERTY(bool canEdit      READ getCanEdit                            NOTIFY canEditChanged)

    Q_PROPERTY(bool isConnected  READ getIsConnected                        NOTIFY connectionTypeChanged)
    Q_PROPERTY(bool isNodeConnection READ getIsNodeConnection               NOTIFY connectionTypeChanged)
    Q_PROPERTY(bool isElectrumConnection READ getIsElectrumConnection       NOTIFY connectionTypeChanged)

public:
    SwapCoinSettingsItem(SwapCoinClientModel& coinClient, beam::wallet::AtomicSwapCoin swapCoin);


    QString getFeeRateLabel() const;
    int getMinFeeRate() const;

    QString getTitle() const;

    int getFeeRate() const;
    void setFeeRate(int value);
    QString getNodeUser() const;
    void setNodeUser(const QString& value);
    QString getNodePass() const;
    void setNodePass(const QString& value);
    QString getNodeAddress() const;
    void setNodeAddress(const QString& value);

    QString getSeedElectrum() const;
    void setSeedElectrum(const QString& value);
    QString getNodeAddressElectrum() const;
    void setNodeAddressElectrum(const QString& value);

    bool getCanEdit() const;

    bool getIsConnected() const;
    bool getIsNodeConnection() const;
    bool getIsElectrumConnection() const;

    Q_INVOKABLE void applyNodeSettings();
    Q_INVOKABLE void applyElectrumSettings();

    Q_INVOKABLE void resetNodeSettings();
    Q_INVOKABLE void resetElectrumSettings();

    Q_INVOKABLE void newElectrumSeed();

    Q_INVOKABLE void disconnect();
    Q_INVOKABLE void connectToNode();
    Q_INVOKABLE void connectToElectrum();

signals:

    void titleChanged();
    void feeRateChanged();
    void nodeUserChanged();
    void nodePassChanged();
    void nodeAddressChanged();

    void seedElectrumChanged();
    void nodeAddressElectrumChanged();

    void useElectrumChanged();
    void canEditChanged();
    void connectionTypeChanged();

private:
    QString getGeneralTitle() const;
    QString getConnectedNodeTitle() const;
    QString getConnectedElectrumTitle() const;

    void LoadSettings();
    void SetDefaultNodeSettings();
    void SetDefaultElectrumSettings();
    void setConnectionType(beam::bitcoin::ISettings::ConnectionType type);

private:
    beam::wallet::AtomicSwapCoin m_swapCoin;
    SwapCoinClientModel& m_coinClient;

    boost::optional<beam::bitcoin::Settings> m_settings;
    int m_feeRate = 0;

    beam::bitcoin::ISettings::ConnectionType m_connectionType = beam::bitcoin::ISettings::None;
    QString m_nodeUser;
    QString m_nodePass;
    QString m_nodeAddress;

    QString m_seedElectrum;
    QString m_nodeAddressElectrum;
};


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

    Q_PROPERTY(QList<QObject*> swapCoinSettings READ getSwapCoinSettings CONSTANT)

public:

    SettingsViewModel();
    virtual ~SettingsViewModel();

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

    const QList<QObject*>& getSwapCoinSettings();

    Q_INVOKABLE uint coreAmount() const;
    Q_INVOKABLE void addLocalNodePeer(const QString& localNodePeer);
    Q_INVOKABLE void deleteLocalNodePeer(int index);
    Q_INVOKABLE void openUrl(const QString& url);
    Q_INVOKABLE void refreshWallet();
    Q_INVOKABLE void openFolder(const QString& path);
    Q_INVOKABLE bool checkWalletPassword(const QString& password) const;

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

protected:
    void timerEvent(QTimerEvent *event) override;

private:
    WalletSettings& m_settings;
    QList<QObject*> m_swapSettings;

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

    const int CHECK_INTERVAL = 1000;
};
