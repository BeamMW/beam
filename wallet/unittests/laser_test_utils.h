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

#include <functional>
#include <memory>

#include "core/treasury.h"
#include "wallet/core/wallet_network.h"
#include "wallet/laser/mediator.h"

using namespace beam::wallet;
using namespace beam;

const uint16_t kDefaultTestNodePort = 32125;
const Height kMaxTestHeight = 180;
const Height kRevisionMaxLifeTime = 71;
const Height kLockTime = 5;
const Height kPostLockReserve = 30;
const Amount kFee = 100;
const Amount kFeeAfter3dFork = 1000000;
const Height kOpenTxDh = 70;
const Height kTestStartBlock = 4;
const unsigned kNewBlockInterval = 200;

struct LaserObserver : public laser::Mediator::Observer
{
    using Action = std::function<void(const laser::ChannelIDPtr& chID)>;
    Action onOpened = [] (const laser::ChannelIDPtr& chID) {};
    Action onOpenFailed = [] (const laser::ChannelIDPtr& chID) {};
    Action onClosed = [] (const laser::ChannelIDPtr& chID) {};
    Action onUpdateFinished = [] (const laser::ChannelIDPtr& chID) {};
    Action onCloseFailed = [] (const laser::ChannelIDPtr& chID) {};
    Action onTransferFailed = Action();
    void OnOpened(const laser::ChannelIDPtr& chID) override
    {
        onOpened(chID);
    }
    void OnOpenFailed(const laser::ChannelIDPtr& chID) override
    {
        onOpenFailed(chID);
    }
    void OnClosed(const laser::ChannelIDPtr& chID) override
    {
        onClosed(chID);
    }
    void OnCloseFailed(const laser::ChannelIDPtr& chID) override
    {
        onCloseFailed(chID);
    }
    void OnUpdateFinished(const laser::ChannelIDPtr& chID) override
    {
        onUpdateFinished(chID);
    }
    void OnTransferFailed(const laser::ChannelIDPtr& chID) override
    {
        onTransferFailed(chID);
    }
};


struct TestLaserInst
    :public laser::Mediator
{
    struct EmptyMsgConsumer :public IWalletMessageConsumer {
        void OnWalletMessage(const WalletID&, const SetTxParameter&) override {}
    } m_EmptyMsgConsumer;

    WalletNetworkViaBbs::Ptr m_pWNet;

    TestLaserInst(const IWalletDB::Ptr& walletDB, const Lightning::Channel::Params& params = {})
        :laser::Mediator(walletDB, params)
    {
        // configure the network

        io::Address nodeAddress = io::Address::localhost().port(kDefaultTestNodePort);
        auto nnet = std::make_shared<proto::FlyClient::NetworkStd>(*this);
        nnet->m_Cfg.m_PollPeriod_ms = 0;
        nnet->m_Cfg.m_vNodes.push_back(nodeAddress);
        nnet->Connect();

        m_pWNet = std::make_shared<WalletNetworkViaBbs>(m_EmptyMsgConsumer, nnet, walletDB);

        SetNetwork(nnet, *m_pWNet, false);
    }

    ~TestLaserInst()
    {
        CloseAll();
    }
};

void MakeTreasuryImpl(const IWalletDB::Ptr& db,
                      const Treasury::Parameters& treasury_params,
                      Treasury& treasury,
                      const AmountList& amounts)
{
    auto kdf = db->get_MasterKdf();

    PeerID pid;
    ECC::Scalar::Native sk;
    Treasury::get_ID(*kdf, pid, sk);

    Treasury::Entry* plan = treasury.CreatePlan(pid, 0, treasury_params);
    beam::Height incubation = 0;
    for (size_t i = 0; i < amounts.size(); ++i)
    {
        if (i == 0)
        {
            plan->m_Request.m_vGroups.front().m_vCoins.front().m_Value = amounts[i];
            incubation = plan->m_Request.m_vGroups.front().m_vCoins.front().m_Incubation;
            continue;
        }

        auto& c = plan->m_Request.m_vGroups.back().m_vCoins.emplace_back();

        c.m_Incubation = incubation;
        c.m_Value = amounts[i];
    }

    plan->m_pResponse.reset(new Treasury::Response);
    uint64_t nIndex = 1;
    plan->m_pResponse->Create(plan->m_Request, *kdf, nIndex);

    for (const auto& group : plan->m_pResponse->m_vGroups)
    {
        for (const auto& treasuryCoin : group.m_vCoins)
        {
            CoinID cid;
            if (treasuryCoin.m_pOutput->Recover(0, *kdf, cid))
            {
                Coin coin;
                coin.m_ID = cid;
                coin.m_maturity = treasuryCoin.m_pOutput->m_Incubation;
                coin.m_confirmHeight = treasuryCoin.m_pOutput->m_Incubation;
                db->saveCoin(coin);
            }
        }
    }
}

ByteBuffer MakeTreasury(
    const IWalletDB::Ptr& db1, const IWalletDB::Ptr& db2,
    Height maturity = 1,
    const AmountList& amounts = {100000000, 100000000, 100000000, 100000000})
{
    Treasury treasury;
    Treasury::Parameters treasury_params;
    treasury_params.m_Bursts = 1;
    treasury_params.m_Maturity0 = maturity;
    treasury_params.m_MaturityStep = 0;

    MakeTreasuryImpl(db1, treasury_params, treasury, amounts);
    MakeTreasuryImpl(db2, treasury_params, treasury, amounts);

    Treasury::Data data;
    data.m_sCustomMsg = "LN";
    treasury.Build(data);

    Serializer ser;
    ser & data;

    ByteBuffer result;
    ser.swap_buf(result);

    return result;
}

void InitTestRules()
{
    Rules::get().pForks[1].m_Height = 1;
    Rules::get().pForks[2].m_Height = 2;
	Rules::get().FakePoW = true;
    Rules::get().MaxRollback = 5;
	Rules::get().UpdateChecksum();
}

constexpr bool is_path_sep(char c)
{
    return c == '/' || c == '\\';
}

constexpr const char* test_name(const char* path)
{
    auto lastname = path;
    for (auto p = path ; *p ; ++p) {
        if (is_path_sep(*p) && *(p+1)) lastname = p+1;
    }
    return lastname;
}
