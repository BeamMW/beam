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
    : m_walletModel{ *AppModel::getInstance()->getWallet() }
    , m_progress{0.0}
    , m_nodeTotal{0}
    , m_nodeDone{0}
    , m_total{0}
    , m_done{0}
    , m_walletConnected{false}
    , m_hasLocalNode{ AppModel::getInstance()->getSettings().getRunLocalNode() }
    , m_skipProgress{false}
    , m_isCreating{false}
{
    connect(&m_walletModel, SIGNAL(syncProgressUpdated(int, int)),
        SLOT(onSyncProgressUpdated(int, int)));

    if (AppModel::getInstance()->getSettings().getRunLocalNode())
    {
        connect(&AppModel::getInstance()->getNode(), SIGNAL(syncProgressUpdated(int, int)),
            SLOT(onNodeSyncProgressUpdated(int, int)));
    }

    connect(&m_walletModel, SIGNAL(nodeConnectionChanged(bool)),
        SLOT(onNodeConnectionChanged(bool)));

    connect(&m_walletModel, SIGNAL(walletError(beam::wallet::ErrorType)),
        SLOT(onGetWalletError(beam::wallet::ErrorType)));

    if (!m_hasLocalNode)
    {
        syncWithNode();
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
    AppModel::getInstance()->resetWallet();
}

void LoadingViewModel::updateProgress()
{
    double nodeSyncProgress = 0.;
	double walletSyncProgress = 0.;

	if (m_nodeTotal > 0)
		nodeSyncProgress = std::min(1., static_cast<double>(m_nodeDone) / static_cast<double>(m_nodeTotal));

	bool bLocalNode = AppModel::getInstance()->getSettings().getRunLocalNode();
	QString progressMessage = tr("");

    if (bLocalNode && (!m_nodeTotal || (m_nodeDone < m_nodeTotal)))
    {
        progressMessage = tr("Downloading blocks");
    }
	else
	{
        if (m_total > 0)
            walletSyncProgress = std::min(1., static_cast<double>(m_done) / static_cast<double>(m_total));

		if (!m_walletConnected)
			syncWithNode();

		if (m_done < m_total)
			progressMessage = QString::asprintf(tr("Scanning UTXO %d/%d").toStdString().c_str(), m_done, m_total);
		else
		{
			emit syncCompleted();
		}
	}

    double p = bLocalNode ? nodeSyncProgress : walletSyncProgress;

    if (p > 0)
    {
        progressMessage.append(QString::asprintf(tr(" %d%%").toStdString().c_str(), static_cast<int>(p * 100)));
    }

    setProgressMessage(progressMessage);
    setProgress(p);

    if (m_skipProgress)
    {
        emit syncCompleted();
    }
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

void LoadingViewModel::syncWithNode()
{
    m_walletModel.getAsync()->syncWithNode();
}

void LoadingViewModel::onNodeConnectionChanged(bool isNodeConnected)
{
    m_walletConnected = isNodeConnected;
}

void LoadingViewModel::onGetWalletError(beam::wallet::ErrorType error)
{
    if (m_isCreating)
    {
        switch (error)
        {
            case beam::wallet::ErrorType::NodeProtocolIncompatible:
            {
                emit walletError(tr("Incompatible peer"), m_walletModel.GetErrorString(error));
                return;
            }
            case beam::wallet::ErrorType::ConnectionAddrInUse:
            {
                emit walletError(tr("Connection error"), m_walletModel.GetErrorString(error));
                return;
            }
            default:
                assert(false && "Unsupported error code!");
        }
    }

    m_skipProgress = true;
    updateProgress();
}