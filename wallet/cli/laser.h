// Copyright 2020 The Beam Team
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

#include "wallet/laser/mediator.h"
#include "utility/common.h"

#include "utility/cli/options.h"
#include <functional>
#include <memory>

namespace beam::wallet
{
    using MediatorPtr = std::unique_ptr<laser::Mediator>;
    class LaserObserver : public laser::Mediator::Observer
    {
    public:
        LaserObserver(const IWalletDB::Ptr& walletDB, const po::variables_map& vm);
        void OnOpened(const laser::ChannelIDPtr& chID) override;
        void OnOpenFailed(const laser::ChannelIDPtr& chID) override;
        void OnClosed(const laser::ChannelIDPtr& chID) override;
        void OnCloseFailed(const laser::ChannelIDPtr& chID) override;
        void OnUpdateStarted(const laser::ChannelIDPtr& chID) override;
        void OnUpdateFinished(const laser::ChannelIDPtr& chID) override;
        void OnTransferFailed(const laser::ChannelIDPtr& chID) override;
        void OnExpired(const laser::ChannelIDPtr& chID) override;
    private:
        const IWalletDB::Ptr& m_walletDB;
        const po::variables_map& m_vm;
    };

    bool LoadLaserParams(const po::variables_map& vm,
                         Amount* aMy,
                         Amount* aTrg,
                         Amount* fee,
                         WalletID* receiverWalletID,
                         bool skipReceiverWalletID = false);
    std::vector<std::string> LoadLaserChannelsIdsFromDB(const IWalletDB::Ptr& walletDB);
    std::vector<std::string> ParseLaserChannelsIdsFromStr(const std::string& chIDsStr);
    const char* LaserChannelStateStr(int state);
    bool LaserOpen(const MediatorPtr& laser,
                   const po::variables_map& vm);
    bool LaserWait(const MediatorPtr& laser,
                   const po::variables_map& vm);
    bool LaserServe(const MediatorPtr& laser,
                    const IWalletDB::Ptr& walletDB,
                    const po::variables_map& vm);
    bool LaserTransfer(const MediatorPtr& laser,
                       const po::variables_map& vm);
    void LaserShow(const IWalletDB::Ptr& walletDB);
    bool LaserDrop(const MediatorPtr& laser,
                   const po::variables_map& vm);
    bool LaserClose(const MediatorPtr& laser,
                    const po::variables_map& vm);
    bool LaserDelete(const MediatorPtr& laser,
                     const po::variables_map& vm);

    bool ProcessLaser(const MediatorPtr& laser,
                      const IWalletDB::Ptr& walletDB,
                      const po::variables_map& vm);
}


