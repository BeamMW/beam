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

#include <cmath>
#include "model/app_model.h"
#include "ui/viewmodel/ui_helpers.h"

#include <qdebug.h>

using namespace beam;
using namespace std;

namespace
{
size_t kFilterRange = 10;
const double kSecondsInMinute = 60.;
const double kSecondsInHour = 60 * 60.;
const double kMaxEstimate = 4 * kSecondsInHour;

const double kRebuildUTXOProgressCoefficient = 0.05;
const double kPercantagePlaceholderThreshold = 0.009;
const char* kPercentagePlaceholderCentesimal = " %.2lf%%";
const char* kPercentagePlaceholderNatural = " %.0lf%%";
const int kMaxTimeDiffForUpdate = 20;
const int kBpsRecessionCountThreshold = 60;

QString getEstimateStr(int estimate)
{
    double value = 0;
    QString units;
    if (estimate >= kSecondsInHour)
    {
        value = ceil(estimate / kSecondsInHour) - 1;
        //% "h."
        units = qtTrId("loading-view-estimate-hours");
        auto res = QString::asprintf(
            "%.0lf %s", value, units.toStdString().c_str());
        value = ceil(
            (estimate - value * kSecondsInHour) / kSecondsInMinute - 1);
        if (value >= 1.)
        {
            //% "min."
            units = qtTrId("loading-view-estimate-minutes");
            res = res + " " + QString::asprintf(
                "%.0lf %s", value, units.toStdString().c_str());
        }
        return res;
    }
    else if (estimate < kSecondsInHour && estimate > 100)
    {
        value = ceil(estimate / kSecondsInMinute);
        units = qtTrId("loading-view-estimate-minutes");
    }
    else if (estimate <= 100 && estimate > kSecondsInMinute)
    {
        value = ceil(estimate / kSecondsInMinute) - 1;
        units = qtTrId("loading-view-estimate-minutes");
        auto res = QString::asprintf(
            "%.0lf %s", value, units.toStdString().c_str());
        value = ceil(estimate - kSecondsInMinute);
        //% "sec."
        units = qtTrId("loading-view-estimate-seconds");
        res = res + " " + QString::asprintf(
            "%.0lf %s", value, units.toStdString().c_str());
        return res;
    }
    else
    {
        value = estimate > 0 ? estimate : 1.;
        units = qtTrId("loading-view-estimate-seconds");
    }
    return QString::asprintf(
        "%.0lf %s", value, units.toStdString().c_str());
}

}  // namespace

Q_DECLARE_METATYPE(uint64_t);

LoadingViewModel::LoadingViewModel()
    : m_walletModel{ *AppModel::getInstance().getWallet() }
    , m_progress{0.0}
    , m_nodeInitProgress{0.}
    , m_total{0}
    , m_done{0}
    , m_lastDone{0}
    , m_hasLocalNode{ AppModel::getInstance().getSettings().getRunLocalNode() }
    , m_isCreating{false}
    , m_isDownloadStarted{false}
    , m_lastProgress{0.}
    , m_bpsWholeTimeFilter(std::make_unique<beamui::Filter>(kFilterRange))
    , m_bpsWindowedFilter(std::make_unique<beamui::Filter>(kFilterRange * 3))
    , m_estimateFilter(std::make_unique<beamui::Filter>(kFilterRange))
    , m_startTimestamp{0}
    , m_lastUpdateTimestamp{0}
    , m_estimate{0}
    , m_bpsRecessionCount{0}
{
    connect(&m_walletModel, SIGNAL(syncProgressUpdated(int, int)), SLOT(onSyncProgressUpdated(int, int)));
    connect(&m_walletModel, SIGNAL(nodeConnectionChanged(bool)), SLOT(onNodeConnectionChanged(bool)));
    connect(&m_walletModel, SIGNAL(walletError(beam::wallet::ErrorType)), SLOT(onGetWalletError(beam::wallet::ErrorType)));

    if (AppModel::getInstance().getSettings().getRunLocalNode())
    {
        connect(&AppModel::getInstance().getNode(), SIGNAL(syncProgressUpdated(int, int)), SLOT(onNodeSyncProgressUpdated(int, int)));
        connect(&AppModel::getInstance().getNode(), SIGNAL(initProgressUpdated(quint64, quint64)), SLOT(onNodeInitProgressUpdated(quint64, quint64)));
    }
}

LoadingViewModel::~LoadingViewModel()
{
}

void LoadingViewModel::onNodeInitProgressUpdated(quint64 done, quint64 total)
{
    m_nodeInitProgress = done / static_cast<double>(total);
}

void LoadingViewModel::onSyncProgressUpdated(int done, int total)
{
    if (!m_hasLocalNode)
    {
        onSync(done, total);
    }
}

void LoadingViewModel::onNodeSyncProgressUpdated(int done, int total)
{
    if (m_hasLocalNode)
    {
        onSync(done, total);
    }
}

void LoadingViewModel::resetWallet()
{
    disconnect(&m_walletModel, SIGNAL(syncProgressUpdated(int, int)), this, SLOT(onSyncProgressUpdated(int, int)));
    disconnect(&m_walletModel, SIGNAL(nodeConnectionChanged(bool)), this, SLOT(onNodeConnectionChanged(bool)));
    disconnect(&m_walletModel, SIGNAL(walletError(beam::wallet::ErrorType)), this, SLOT(onGetWalletError(beam::wallet::ErrorType)));
    connect(&AppModel::getInstance(), SIGNAL(walletResetCompleted()), this, SIGNAL(walletResetCompleted()));
    AppModel::getInstance().resetWallet();
}

void LoadingViewModel::recalculateProgress()
{
    updateProgress();
}

void LoadingViewModel::onSync(int done, int total)
{
    if (!m_isDownloadStarted)
    {
        m_done = 0;
        m_total = 0;
        m_startTimestamp = getTimestamp();
        m_isDownloadStarted = true;
    }
    m_previousUpdateTimestamp = m_lastUpdateTimestamp;
    m_lastUpdateTimestamp = getTimestamp();
    m_lastDone = m_done;
    m_done = done;
    m_total = total;
}

void LoadingViewModel::updateProgress()
{
    double progress = 0.;
	QString progressMessage = "";
    //% "Estimate time: %s"
    QString estimateStr = qtTrId("loading-view-estimate-time");
    //% "calculating..."
    QString calculating = qtTrId("loading-view-estimate-calculating");

    if (m_isDownloadStarted)
    {
        if (m_total > 0)
        {
            progress = std::min(1., m_done / static_cast<double>(m_total));
        }

        if (m_hasLocalNode)
        {
            //% "Synching with blockchain"
            progressMessage = qtTrId("loading-view-download-blocks");
        }
        else
        {
            //% "Loading wallet data %d/%d"
            progressMessage = QString::asprintf(
                qtTrId("loading-view-scaning-utxo").toStdString().c_str(),
                m_done,
                m_total);
        }

        progress = kRebuildUTXOProgressCoefficient +
                   progress * (1.0 - kRebuildUTXOProgressCoefficient);

        auto wbps = getWindowedBps();
        auto bps = (getWholeTimeBps() + wbps) / 2;

        if (!bps)
        {
            estimateStr = QString::asprintf(
                    estimateStr.toStdString().c_str(),
                    calculating.toStdString().c_str());
        }
        else if (detectNetworkProblems())
        {
            //% "It may take longer then usual. Please, check your network."
            estimateStr = qtTrId("loading-view-net-problems");
        }
        else
        {
            m_estimate = getEstimate(bps);
            estimateStr = QString::asprintf(
                    estimateStr.toStdString().c_str(),
                    getEstimateStr(m_estimate).toStdString().c_str());
        }

        if (m_done >= m_total)
        {
            emit syncCompleted();
        }
    }
    else
    {
       m_hasLocalNode = AppModel::getInstance().getSettings().getRunLocalNode();
        if (m_hasLocalNode)
        {
            //% "Rebuilding wallet data"
            progressMessage = qtTrId("loading-view-rebuild-utxos");
            progress = kRebuildUTXOProgressCoefficient * m_nodeInitProgress;
        }
        estimateStr = QString::asprintf(
                estimateStr.toStdString().c_str(),
                calculating.toStdString().c_str());       
    }
   
    if (progress < m_lastProgress)
        progress = m_lastProgress;

    progressMessage.append(
        QString::asprintf(getPercentagePlaceholder(progress), progress * 100));
    if (m_isDownloadStarted)
    {
        progressMessage.append(" " + estimateStr);
    }

    setProgressMessage(progressMessage);
    setProgress(progress);
}

const char* LoadingViewModel::getPercentagePlaceholder(double progress) const
{
    return (progress > 0
           && static_cast<int>(ceil(progress * 10000)) % 100
           && (progress - m_lastProgress) <= kPercantagePlaceholderThreshold)
        ? kPercentagePlaceholderCentesimal
        : kPercentagePlaceholderNatural;
}

int LoadingViewModel::getEstimate(double bps)
{
    m_estimateFilter->addSample((m_total - m_done) / bps);
    auto estimate = m_estimateFilter->getMedian();
    if (estimate > kMaxEstimate)
    {
        return kMaxEstimate;
    }
    else if (estimate < 2 * kSecondsInMinute)
    {
        return ceil(m_estimateFilter->getAverage());
    }
    else
    {
        return estimate;
    }
}

double LoadingViewModel::getWholeTimeBps() const
{
    if (!m_done)
        return 0.;

    auto timeDiff = getTimestamp() - m_startTimestamp + 1;
    m_bpsWholeTimeFilter->addSample(m_done / static_cast<double>(timeDiff));
    return m_bpsWholeTimeFilter->getMedian();
}

double LoadingViewModel::getWindowedBps() const
{
    if (!m_done)
        return 0.;

    auto timeDiff = m_lastUpdateTimestamp -
        (m_previousUpdateTimestamp
            ? m_previousUpdateTimestamp
            : m_startTimestamp);
    if (timeDiff < 1)
        timeDiff = 1;
    m_bpsWindowedFilter->addSample(
        (m_done - m_lastDone) / static_cast<double>(timeDiff));
    return m_bpsWindowedFilter->getAverage();
}

bool LoadingViewModel::detectNetworkProblems()
{
    auto possibleWaitTime =
        m_lastUpdateTimestamp -
        m_previousUpdateTimestamp + kMaxTimeDiffForUpdate;
    if ((getTimestamp() - m_lastUpdateTimestamp) > possibleWaitTime)
    {
        ++m_bpsRecessionCount;
    }
    else
    {
        m_bpsRecessionCount = 0;
    }

    return m_bpsRecessionCount > kBpsRecessionCountThreshold;
}

double LoadingViewModel::getProgress() const
{
    return m_progress;
}

void LoadingViewModel::setProgress(double value)
{
    if (value > m_progress)
    {
        m_lastProgress = m_progress;
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

