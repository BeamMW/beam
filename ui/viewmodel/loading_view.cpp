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

#include "loading_view.h"
#include "model/app_model.h"

using namespace beam;
using namespace std;

LoadingViewModel::LoadingViewModel()
    : m_walletModel{ *AppModel::getInstance().getWallet() }
    , m_progress{0.0}
    , m_nodeTotal{0}
    , m_nodeDone{0}
    , m_total{0}
    , m_done{0}
    , m_hasLocalNode{ AppModel::getInstance().getSettings().getRunLocalNode() }
    , m_isCreating{false}
{
    connect(&m_walletModel, SIGNAL(syncProgressUpdated(int, int)), SLOT(onSyncProgressUpdated(int, int)));
    connect(&m_walletModel, SIGNAL(nodeConnectionChanged(bool)), SLOT(onNodeConnectionChanged(bool)));
    connect(&m_walletModel, SIGNAL(walletError(beam::wallet::ErrorType)), SLOT(onGetWalletError(beam::wallet::ErrorType)));

    if (AppModel::getInstance().getSettings().getRunLocalNode())
    {
        connect(&AppModel::getInstance().getNode(), SIGNAL(syncProgressUpdated(int, int)), SLOT(onNodeSyncProgressUpdated(int, int)));
    }
}

LoadingViewModel::~LoadingViewModel()
{
}

void LoadingViewModel::onSyncProgressUpdated(int done, int total)
{
    m_done = done;
    m_total = total;
    updateProgress();
}

void LoadingViewModel::onNodeSyncProgressUpdated(int done, int total)
{
    m_nodeDone = done;
    m_nodeTotal = total;
    updateProgress();
}

void LoadingViewModel::resetWallet()
{
    disconnect(&m_walletModel, SIGNAL(syncProgressUpdated(int, int)), this, SLOT(onSyncProgressUpdated(int, int)));
    disconnect(&m_walletModel, SIGNAL(nodeConnectionChanged(bool)), this, SLOT(onNodeConnectionChanged(bool)));
    disconnect(&m_walletModel, SIGNAL(walletError(beam::wallet::ErrorType)), this, SLOT(onGetWalletError(beam::wallet::ErrorType)));
    connect(&AppModel::getInstance(), SIGNAL(walletReseted()), this, SLOT(onWalletReseted()));
    AppModel::getInstance().resetWallet();
}

void LoadingViewModel::updateProgress()
{
    double nodeSyncProgress = 0.;
	double walletSyncProgress = 0.;

	if (m_nodeTotal > 0)
		nodeSyncProgress = std::min(1., static_cast<double>(m_nodeDone) / static_cast<double>(m_nodeTotal));

	bool bLocalNode = AppModel::getInstance().getSettings().getRunLocalNode();
	QString progressMessage = "";

    if (bLocalNode && (!m_nodeTotal || (m_nodeDone < m_nodeTotal)))
    {
        //% "Downloading blocks"
        progressMessage = qtTrId("loading-view-download-blocks");
    }
	else
	{
        if (m_total > 0)
            walletSyncProgress = std::min(1., static_cast<double>(m_done) / static_cast<double>(m_total));

		if (m_done < m_total)
        {
            //% "Scanning UTXO %d/%d"
			progressMessage = QString::asprintf(qtTrId("loading-view-scaning-utxo").toStdString().c_str(), m_done, m_total);
        }
		else
		{
			emit syncCompleted();
		}
	}

    double p = bLocalNode ? nodeSyncProgress : walletSyncProgress;

    if (p > 0)
    {
        progressMessage.append(QString::asprintf(" %d%%", static_cast<int>(p * 100)));
    }

    setProgressMessage(progressMessage);
    setProgress(p);
}

double LoadingViewModel::getProgress() const
{
    return m_progress;
}

void LoadingViewModel::setProgress(double value)
{
    if (value > m_progress)
    {
        m_progress = value;
        emit progressChanged();
    }
}

const QString& LoadingViewModel::getProgressMessage() const
{
    return m_progressMessage;
}

void LoadingViewModel::setProgressMessage(const QString& value)
{
    if (m_progressMessage != value)
    {
        m_progressMessage = value;
        emit progressMessageChanged();
    }
}

void LoadingViewModel::setIsCreating(bool value)
{
    if (m_isCreating != value)
    {
        m_isCreating = value;
        emit isCreatingChanged();
    }
}

bool LoadingViewModel::getIsCreating() const
{
    return m_isCreating;
}

void LoadingViewModel::onNodeConnectionChanged(bool isNodeConnected)
{
}

void LoadingViewModel::onGetWalletError(beam::wallet::ErrorType error)
{
    if (m_isCreating)
    {
        switch (error)
        {
            case beam::wallet::ErrorType::NodeProtocolIncompatible:
            {
                //% "Incompatible peer"
                emit walletError(qtTrId("loading-view-protocol-error"), m_walletModel.GetErrorString(error));
                return;
            }
            case beam::wallet::ErrorType::ConnectionAddrInUse:
            case beam::wallet::ErrorType::ConnectionRefused:
            case beam::wallet::ErrorType::HostResolvedError:
            {
                //% "Connection error"
                emit walletError(qtTrId("loading-view-connection-error"), m_walletModel.GetErrorString(error));
                return;
            }
            default:
                assert(false && "Unsupported error code!");
        }
    }

    // For task 721. For now we're handling only port error.
    // There rest need to be added later
    switch (error)
    {
        case beam::wallet::ErrorType::ConnectionAddrInUse:
            emit walletError(qtTrId("loading-view-connection-error"), m_walletModel.GetErrorString(error));
            return;
        default:
            break;
    }

    // There's an unhandled error. Show wallet and display it in errorneous state
    updateProgress();
    emit syncCompleted();
}

void LoadingViewModel::onWalletReseted()
{
    emit walletReseted();
}