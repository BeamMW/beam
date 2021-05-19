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

#include "laser.h"
#include "wallet/core/strings_resources.h"

#include <boost/format.hpp>
#include <iomanip>

using namespace std;

namespace beam::wallet
{

LaserObserver::LaserObserver(const IWalletDB::Ptr& walletDB,
                             const po::variables_map& vm)
    : m_walletDB(walletDB), m_vm(vm) {}

void LaserObserver::OnOpened(const laser::ChannelIDPtr& chID)
{
    if (m_vm.count(cli::LASER_OPEN))
    {
        io::Reactor::get_Current().stop();
    }
    else if (m_vm.count(cli::LASER_WAIT))
    {
        LOG_INFO() << boost::format(kLaserMessageChannelServed)
                   % to_hex(chID->m_pData, chID->nBytes);
    }
    LaserShow(m_walletDB);
}

void LaserObserver::OnOpenFailed(const laser::ChannelIDPtr& chID)
{
    LOG_ERROR() << boost::format(kLaserErrorOpenFailed)
                % to_hex(chID->m_pData, chID->nBytes);
    io::Reactor::get_Current().stop();
}

void LaserObserver::OnClosed(const laser::ChannelIDPtr& chID)
{
    if (!m_observable->getChannelsCount())
    {
        io::Reactor::get_Current().stop();
    }
    LOG_INFO() << boost::format(kLaserMessageClosed)
               % to_hex(chID->m_pData, chID->nBytes);
    LaserShow(m_walletDB); 
}

void LaserObserver::OnCloseFailed(const laser::ChannelIDPtr& chID)
{
    if (m_vm.count(cli::LASER_CLOSE_GRACEFUL))
    {
        io::Reactor::get_Current().stop();
    }
    LOG_ERROR() << boost::format(kLaserMessageCloseFailed)
                % to_hex(chID->m_pData, chID->nBytes);
}

void LaserObserver::OnUpdateStarted(const laser::ChannelIDPtr& chID)
{
}

void LaserObserver::OnUpdateFinished(const laser::ChannelIDPtr& chID)
{
    if (m_vm.count(cli::LASER_TRANSFER))
    {
        io::Reactor::get_Current().stop();
    }
    LOG_INFO() << boost::format(kLaserMessageUpdateFinished)
               % to_hex(chID->m_pData, chID->nBytes);
}

void LaserObserver::OnTransferFailed(const laser::ChannelIDPtr& chID)
{
    LOG_ERROR() << boost::format(kLaserErrorTransferFailed)
                % to_hex(chID->m_pData, chID->nBytes);
}

void LaserObserver::OnExpired(const laser::ChannelIDPtr& chID)
{
    if (!m_observable->getChannelsCount())
    {
        io::Reactor::get_Current().stop();
    }
    LOG_INFO() << boost::format(kLaserMessageExpired)
               % to_hex(chID->m_pData, chID->nBytes);
    LaserShow(m_walletDB); 
}

bool LoadLaserParams(const po::variables_map& vm, laser::Mediator& mediator,
                     Amount* aMy,
                     Amount* aTrg,
                     Amount* fee,
                     WalletID* receiverWalletID,
                     bool skipReceiverWalletID)
{
    if (!skipReceiverWalletID)
    {
        if (!vm.count(cli::LASER_TARGET_ADDR))
        {
            LOG_ERROR() << kErrorReceiverAddrMissing;
            return false;
        }
        receiverWalletID->FromHex(vm[cli::LASER_TARGET_ADDR].as<string>());
    }    

    if (!vm.count(cli::LASER_AMOUNT_MY))
    {
        LOG_ERROR() << kLaserErrorMyAmountMissing;
        return false;
    }

    if (!vm.count(cli::LASER_AMOUNT_TARGET))
    {
        LOG_ERROR() << kLaserErrorTrgAmountMissing;
        return false;
    }

    auto myAmount = vm[cli::LASER_AMOUNT_MY].as<NonnegativeFloatingPoint<double>>().value;
    myAmount *= Rules::Coin;
    *aMy = static_cast<ECC::Amount>(std::round(myAmount));

    auto trgAmount = vm[cli::LASER_AMOUNT_TARGET].as<NonnegativeFloatingPoint<double>>().value;
    trgAmount *= Rules::Coin;
    *aTrg = static_cast<ECC::Amount>(std::round(trgAmount));

    if (*aMy == 0 && *aTrg == 0)
    {
        LOG_ERROR() << "My amount and Remote side amount are Zero";
        return false;
    }

    Block::SystemState::Full s;
    mediator.getWalletDB()->get_History().get_Tip(s);
    auto& fs = Transaction::FeeSettings::get(s.m_Height);
    Amount feeMin = fs.get_DefaultStd();

    if (vm.count(cli::LASER_FEE))
    {
        *fee = vm[cli::LASER_FEE].as<Nonnegative<Amount>>().value;
        if (*fee < feeMin)
        {
            LOG_ERROR() << "Failed to initiate the operation. The minimum fee is 100 groth.";
            return false;
        }
    }
    else
    {
        LOG_INFO() << "\"--" << cli::LASER_FEE << "\" param is not specified, using default fee = " << feeMin;
        *fee = feeMin;
    }

    return true;
}

std::vector<std::string> LoadLaserChannelsIdsFromDB(
        const IWalletDB::Ptr& walletDB)
{
    std::vector<std::string> channelIDs;
    auto chDBEntities = walletDB->loadLaserChannels();
    channelIDs.reserve(chDBEntities.size());
    for (auto& ch : chDBEntities)
    {
        const auto& chID = std::get<LaserFields::LASER_CH_ID>(ch);
        channelIDs.emplace_back(
            beam::to_hex(chID.m_pData, chID.nBytes));
    }

    return channelIDs;
}

std::vector<std::string> ParseLaserChannelsIdsFromStr(
        const std::string& chIDsStr)
{
    std::vector<std::string> channelIDs;
    std::stringstream ss(chIDsStr);
    std::string chId;
    while (std::getline(ss, chId, ','))
        channelIDs.push_back(chId);

    return channelIDs;
}

const char* LaserChannelStateStr(int state)
{
    switch(state)
    {
    case Lightning::Channel::State::None:
    case Lightning::Channel::State::Opening0:
        return kLaserWaitPeer;
    case Lightning::Channel::State::Opening1:
    case Lightning::Channel::State::Opening2:
        return kLaserOpening;
    case Lightning::Channel::State::OpenFailed:
        return kLaserOpenFailed;
    case Lightning::Channel::State::Open:
        return kLaserOpen;
    case Lightning::Channel::State::Updating:
        return kLaserUpdating;
    case Lightning::Channel::State::Closing1:
    case Lightning::Channel::State::Closing2:
        return kLaserClosing;
    case Lightning::Channel::State::Closed:
        return kLaserClosed;
    case Lightning::Channel::State::Expired:
        return kLaserExpired;
    default:
        return kLaserUnknown;
    }
}

bool LaserOpen(const MediatorPtr& laser,
               const po::variables_map& vm)
{
    io::Address receiverAddr;
    Amount aMy = 0, aTrg = 0, fee = 0;
    WalletID receiverWalletID(Zero);

    if (!LoadLaserParams(vm, *laser, &aMy, &aTrg, &fee, &receiverWalletID))
    {
        LOG_ERROR() << kLaserErrorParamsRead;
        return false;
    }

    laser->OpenChannel(aMy, aTrg, fee, receiverWalletID);
    return true;
}

bool LaserWait(const MediatorPtr& laser,
               const po::variables_map& vm)
{
    io::Address receiverAddr;
    Amount aMy = 0, aTrg = 0, fee = 0;
    WalletID receiverWalletID(Zero);

    if (!LoadLaserParams(vm, *laser, &aMy, &aTrg, &fee, &receiverWalletID, true))
    {
        LOG_ERROR() << kLaserErrorParamsRead;
        return false;
    }

    laser->WaitIncoming(aMy, aTrg, fee);
    return true;
}

bool LaserServe(const MediatorPtr& laser,
                const IWalletDB::Ptr& walletDB,
                const po::variables_map& vm)
{
    auto channelIDsStr = vm[cli::LASER_SERVE].as<string>();
    auto channelIDs = channelIDsStr.empty()
        ? LoadLaserChannelsIdsFromDB(walletDB)
        : ParseLaserChannelsIdsFromStr(channelIDsStr);

    if (channelIDs.empty())
    {
        LOG_ERROR() << "Channels not specified";
        return false;
    }

    uint64_t count = 0;
    for (const auto& channelID: channelIDs)
    {
        if (laser->Serve(channelID)) ++count;
    }

    LOG_INFO() << "Listen: " << count
               << (count == 1 ? " channel" : " channels");
    return count != 0;
}

bool LaserTransfer(const MediatorPtr& laser,
                   const po::variables_map& vm)
{
    if (!vm.count(cli::LASER_CHANNEL_ID))
    {
        LOG_ERROR() << kLaserErrorChannelIdMissing;
        return false;
    }

    auto myAmount = vm[cli::LASER_TRANSFER].as<Positive<double>>().value;
    myAmount *= Rules::Coin;
    Amount amount = static_cast<ECC::Amount>(std::round(myAmount));
    if (!amount)
    {
        LOG_ERROR() << kErrorZeroAmount;
        return false;
    }

    auto chIdStr = vm[cli::LASER_CHANNEL_ID].as<string>();

    return laser->Transfer(amount, chIdStr);  
}

void LaserShow(const IWalletDB::Ptr& walletDB)
{
    auto channels = walletDB->loadLaserChannels();
    if (channels.empty())
    {
        LOG_INFO() << "You has no laser channels yet";
        return;
    }

    array<uint8_t, 6> columnWidths{ { 32, 10, 10, 10, 10, 8 } };

    // channel Id | aMy | aTrg | state | fee | valid till
    cout << boost::format(kLaserChannelListTableHead)
            % boost::io::group(left, setw(columnWidths[0]), kLaserChannelListChannelId)
            % boost::io::group(left, setw(columnWidths[1]), kLaserChannelListAMy)
            % boost::io::group(left, setw(columnWidths[2]), kLaserChannelListATrg)
            % boost::io::group(left, setw(columnWidths[3]), kLaserChannelListState)
            % boost::io::group(left, setw(columnWidths[4]), kLaserChannelListFee)
            % boost::io::group(left, setw(columnWidths[5]), kLaserChannelListValidTill)
            << std::endl;

    for (auto& ch : channels)
    {
        const auto& chID = std::get<LaserFields::LASER_CH_ID>(ch);
        auto state = std::get<LaserFields::LASER_STATE>(ch);

        cout << boost::format(kLaserChannelTableBody)
            % boost::io::group(left, setw(columnWidths[0]), beam::to_hex(chID.m_pData, chID.nBytes))
            % boost::io::group(left, setw(columnWidths[1]), to_string(PrintableAmount(std::get<LaserFields::LASER_AMOUNT_CURRENT_MY>(ch), true)))
            % boost::io::group(left, setw(columnWidths[2]), to_string(PrintableAmount(std::get<LaserFields::LASER_AMOUNT_CURRENT_TRG>(ch), true)))
            % boost::io::group(left, setw(columnWidths[3]), LaserChannelStateStr(state))
            % boost::io::group(left, setw(columnWidths[4]), to_string(PrintableAmount(std::get<LaserFields::LASER_FEE>(ch), true)))
            % boost::io::group(left, setw(columnWidths[5]), state == Lightning::Channel::State::Open ? std::get<LaserFields::LASER_LOCK_HEIGHT>(ch) : 0)
            << std::endl;
    }
}

bool LaserDrop(const MediatorPtr& laser,
               const po::variables_map& vm)
{
    auto channelIDsStr = vm[cli::LASER_DROP].as<string>();
    auto channelIDs = ParseLaserChannelsIdsFromStr(channelIDsStr);

    if (channelIDs.empty())
    {
        LOG_ERROR() << "Channels not specified";
        return false;
    }

    uint64_t count = 0;
    for (const auto& channelID: channelIDs)
    {
        if (laser->Close(channelID)) ++count;
    }

    LOG_INFO() << "Close sceduled for: " << count
               << (count == 1 ? " channel" : " channels");
    return count != 0;
}

bool LaserClose(const MediatorPtr& laser,
                const po::variables_map& vm)
{
    auto channelIDsStr = vm[cli::LASER_CLOSE_GRACEFUL].as<string>();
    auto channelIDs = ParseLaserChannelsIdsFromStr(channelIDsStr);
    
    if (channelIDs.empty())
    {
        LOG_ERROR() << "Channels not specified";
        return false;
    }

    uint64_t count = 0;
    for (const auto& channelID: channelIDs)
    {
        if (laser->GracefulClose(channelID)) ++count;
    }

    LOG_INFO() << "Close sceduled for: " << count
               << (count == 1 ? " channel" : " channels");
    return count != 0;
}

bool LaserDelete(const MediatorPtr& laser,
                 const po::variables_map& vm)
{
    auto channelIDsStr = vm[cli::LASER_DELETE].as<string>();
    auto channelIDs = ParseLaserChannelsIdsFromStr(channelIDsStr);

    if (channelIDs.empty())
    {
        LOG_ERROR() << "Channels not specified";
        return false;
    }

    uint64_t count = 0;
    for (const auto& channelID: channelIDs)
    {
        if (laser->Delete(channelID)) ++count;
    }

    LOG_INFO() << "Deleted: " << count
               << (count == 1 ? " channel" : " channels");
    return count != 0;
}

bool ProcessLaser(const MediatorPtr& laser,
                  const IWalletDB::Ptr& walletDB,
                  const po::variables_map& vm)
{
    if (vm.count(cli::LASER_OPEN))
    {
        return LaserOpen(laser, vm);
    }
    else if (vm.count(cli::LASER_WAIT))
    {
        return LaserWait(laser, vm);
    }
    else if (vm.count(cli::LASER_SERVE))
    {
        return LaserServe(laser, walletDB, vm);
    }
    else if (vm.count(cli::LASER_TRANSFER))
    {
        return LaserTransfer(laser, vm);
    }
    else if (vm.count(cli::LASER_DROP))
    {
        return LaserDrop(laser, vm);
    }
    else if (vm.count(cli::LASER_CLOSE_GRACEFUL))
    {
        return LaserClose(laser, vm);
    }

    return false;
}

}
