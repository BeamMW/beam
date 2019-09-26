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

    Q_PROPERTY(QString  btcUser        READ getBtcUser         WRITE setBtcUser         NOTIFY btcUserChanged)
    Q_PROPERTY(QString  btcPass        READ getBtcPass         WRITE setBtcPass         NOTIFY btcPassChanged)
    Q_PROPERTY(QString  btcNodeAddress READ getBtcNodeAddress  WRITE setBtcNodeAddress  NOTIFY btcNodeAddressChanged)
    Q_PROPERTY(int      btcFeeRate     READ getBtcFeeRate      WRITE setBtcFeeRate      NOTIFY btcFeeRateChanged)

    Q_PROPERTY(QString  ltcUser        READ getLtcUser         WRITE setLtcUser         NOTIFY ltcUserChanged)
    Q_PROPERTY(QString  ltcPass        READ getLtcPass         WRITE setLtcPass         NOTIFY ltcPassChanged)
    Q_PROPERTY(QString  ltcNodeAddress READ getLtcNodeAddress  WRITE setLtcNodeAddress  NOTIFY ltcNodeAddressChanged)
    Q_PROPERTY(int      ltcFeeRate     READ getLtcFeeRate      WRITE setLtcFeeRate      NOTIFY ltcFeeRateChanged)

    Q_PROPERTY(QString  qtumUser        READ getQtumUser         WRITE setQtumUser         NOTIFY qtumUserChanged)
    Q_PROPERTY(QString  qtumPass        READ getQtumPass         WRITE setQtumPass         NOTIFY qtumPassChanged)
    Q_PROPERTY(QString  qtumNodeAddress READ getQtumNodeAddress  WRITE setQtumNodeAddress  NOTIFY qtumNodeAddressChanged)
    Q_PROPERTY(int      qtumFeeRate     READ getQtumFeeRate      WRITE setQtumFeeRate      NOTIFY qtumFeeRateChanged)

    Q_PROPERTY(QString  btcSeedEL        READ getBtcSeedEL         WRITE setBtcSeedEL         NOTIFY btcSeedELChanged)
    Q_PROPERTY(QString  btcNodeAddressEL READ getBtcNodeAddressEL  WRITE setBtcNodeAddressEL  NOTIFY btcNodeAddressELChanged)
    Q_PROPERTY(int      btcFeeRateEL     READ getBtcFeeRateEL      WRITE setBtcFeeRateEL      NOTIFY btcFeeRateELChanged)

    Q_PROPERTY(QString  ltcSeedEL        READ getLtcSeedEL         WRITE setLtcSeedEL         NOTIFY ltcSeedELChanged)
    Q_PROPERTY(QString  ltcNodeAddressEL READ getLtcNodeAddressEL  WRITE setLtcNodeAddressEL  NOTIFY ltcNodeAddressELChanged)
    Q_PROPERTY(int      ltcFeeRateEL     READ getLtcFeeRateEL      WRITE setLtcFeeRateEL      NOTIFY ltcFeeRateELChanged)

    Q_PROPERTY(QString  qtumSeedEL        READ getQtumSeedEL         WRITE setQtumSeedEL         NOTIFY qtumSeedELChanged)
    Q_PROPERTY(QString  qtumNodeAddressEL READ getQtumNodeAddressEL  WRITE setQtumNodeAddressEL  NOTIFY qtumNodeAddressELChanged)
    Q_PROPERTY(int      qtumFeeRateEL     READ getQtumFeeRateEL      WRITE setQtumFeeRateEL      NOTIFY qtumFeeRateELChanged)

     Q_PROPERTY(bool btcUseEL   READ getBtcUseEL    WRITE setBtcUseEL    NOTIFY btcUseELChanged)
     Q_PROPERTY(bool ltcUseEL   READ getLtcUseEL    WRITE setLtcUseEL    NOTIFY ltcUseELChanged)
     Q_PROPERTY(bool qtumUseEL  READ getQtumUseEL   WRITE setQtumUseEL   NOTIFY qtumUseELChanged)


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

    QString getBtcUser() const;
    void setBtcUser(const QString& value);
    QString getBtcPass() const;
    void setBtcPass(const QString& value);
    QString getBtcNodeAddress() const;
    void setBtcNodeAddress(const QString& value);
    int getBtcFeeRate() const;
    void setBtcFeeRate(int value);

    QString getLtcUser() const;
    void setLtcUser(const QString& value);
    QString getLtcPass() const;
    void setLtcPass(const QString& value);
    QString getLtcNodeAddress() const;
    void setLtcNodeAddress(const QString& value);
    int getLtcFeeRate() const;
    void setLtcFeeRate(int value);

    QString getBtcSeedEL() const;
    void setBtcSeedEL(const QString& value);
    QString getBtcNodeAddressEL() const;
    void setBtcNodeAddressEL(const QString& value);
    int getBtcFeeRateEL() const;
    void setBtcFeeRateEL(int value);

    QString getLtcSeedEL() const;
    void setLtcSeedEL(const QString& value);
    QString getLtcNodeAddressEL() const;
    void setLtcNodeAddressEL(const QString& value);
    int getLtcFeeRateEL() const;
    void setLtcFeeRateEL(int value);

    QString getQtumSeedEL() const;
    void setQtumSeedEL(const QString& value);
    QString getQtumNodeAddressEL() const;
    void setQtumNodeAddressEL(const QString& value);
    int getQtumFeeRateEL() const;
    void setQtumFeeRateEL(int value);

    bool getBtcUseEL() const;
    void setBtcUseEL(bool value);

    bool getLtcUseEL() const;
    void setLtcUseEL(bool value);

    bool getQtumUseEL() const;
    void setQtumUseEL(bool value);

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
    Q_INVOKABLE void applyBtcSettingsEL();
    Q_INVOKABLE void applyLtcSettingsEL();
    Q_INVOKABLE void applyQtumSettingsEL();

    Q_INVOKABLE void btcOff();
    Q_INVOKABLE void ltcOff();
    Q_INVOKABLE void qtumOff();
    Q_INVOKABLE void btcOffEL();
    Q_INVOKABLE void ltcOffEL();
    Q_INVOKABLE void qtumOffEL();

    Q_INVOKABLE void btcNewSeedEL();
    Q_INVOKABLE void ltcNewSeedEL();
    Q_INVOKABLE void qtumNewSeedEL();

    QString getQtumUser() const;
    void setQtumUser(const QString& value);
    QString getQtumPass() const;
    void setQtumPass(const QString& value);
    QString getQtumNodeAddress() const;
    void setQtumNodeAddress(const QString& value);
    int getQtumFeeRate() const;
    void setQtumFeeRate(int value);

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

    void btcSeedELChanged();
    void btcNodeAddressELChanged();
    void btcFeeRateELChanged();

    void ltcSeedELChanged();
    void ltcNodeAddressELChanged();
    void ltcFeeRateELChanged();

    void qtumSeedELChanged();
    void qtumNodeAddressELChanged();
    void qtumFeeRateELChanged();

    void ltcUserChanged();
    void ltcPassChanged();
    void ltcNodeAddressChanged();
    void ltcFeeRateChanged();

    void qtumUserChanged();
    void qtumPassChanged();
    void qtumNodeAddressChanged();
    void qtumFeeRateChanged();

    void btcUseELChanged();
    void ltcUseELChanged();
    void qtumUseELChanged();

protected:
    void timerEvent(QTimerEvent *event) override;

private:
    void LoadBitcoinSettings();
    void LoadLitecoinSettings();
    void LoadQtumSettings();

    void SetDefaultBtcSettings();
    void SetDefaultBtcSettingsEL();
    void SetDefaultLtcSettings();
    void SetDefaultLtcSettingsEL();
    void SetDefaultQtumSettings();
    void SetDefaultQtumSettingsEL();

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

    boost::optional<beam::bitcoin::Settings> m_bitcoinSettings;
    QString m_bitcoinUser;
    QString m_bitcoinPass;
    QString m_bitcoinNodeAddress;
    int m_bitcoinFeeRate = 0;

    QString m_bitcoinSeedEl;
    QString m_bitcoinNodeAddressEl;
    int m_bitcoinFeeRateEl = 0;

    boost::optional<beam::bitcoin::Settings> m_litecoinSettings;
    QString m_litecoinUser;
    QString m_litecoinPass;
    QString m_litecoinNodeAddress;
    int m_litecoinFeeRate = 0;

    QString m_litecoinSeedEl;
    QString m_litecoinNodeAddressEl;
    int m_litecoinFeeRateEl = 0;

    boost::optional<beam::bitcoin::Settings> m_qtumSettings;
    QString m_qtumUser;
    QString m_qtumPass;
    QString m_qtumNodeAddress;
    int m_qtumFeeRate = 0;

    QString m_qtumSeedEl;
    QString m_qtumNodeAddressEl;
    int m_qtumFeeRateEl = 0;

    bool m_btcUseEL = false;
    bool m_ltcUseEL = false;
    bool m_qtumUseEL = false;

    const int CHECK_INTERVAL = 1000;
};
