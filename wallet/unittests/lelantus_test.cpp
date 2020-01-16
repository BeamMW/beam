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

#include "wallet/core/common.h"
#include "wallet/core/wallet_network.h"
#include "wallet/core/wallet.h"
#include "wallet/transactions/lelantus/push_transaction.h"
#include "keykeeper/local_private_key_keeper.h"
#include "wallet/core/simple_transaction.h"
#include "core/unittest/mini_blockchain.h"
#include "utility/test_helpers.h"

#include "node/node.h"

#include "test_helpers.h"

#include <boost/filesystem.hpp>

using namespace beam;
using namespace std;
using namespace ECC;

WALLET_TEST_INIT

#include "wallet_test_environment.cpp"

namespace
{
    const AmountList kDefaultTestAmounts = { 5000, 2000, 1000, 9000 };
}

void TestSimpleTx()
{
    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    auto completeAction = [&mainReactor](auto)
    {
        mainReactor->stop();
    };

    auto senderWalletDB = createSenderWalletDB(0, 0);
    auto binaryTreasury = createTreasury(senderWalletDB, kDefaultTestAmounts);
    TestWalletRig sender("sender", senderWalletDB, completeAction, TestWalletRig::RegularWithoutPoWBbs);

    auto creator = std::make_shared<lelantus::PushTransaction::Creator>();
    sender.m_Wallet.RegisterTransactionType(TxType::PushTransaction, std::static_pointer_cast<BaseTransaction::Creator>(creator));

    Node node;

    NodeObserver observer([&]()
    {
        auto cursor = node.get_Processor().m_Cursor;
        if (cursor.m_Sid.m_Height == Rules::get().pForks[2].m_Height + 3)
        {
            wallet::TxParameters parameters(GenerateTxID());

            parameters.SetParameter(TxParameterID::TransactionType, TxType::PushTransaction)
                .SetParameter(TxParameterID::Amount, 3800)
                .SetParameter(TxParameterID::Fee, 1200)
                .SetParameter(TxParameterID::MyID, sender.m_WalletID)
                .SetParameter(TxParameterID::Lifetime, kDefaultTxLifetime)
                .SetParameter(TxParameterID::PeerResponseTime, kDefaultTxResponseTime)
                .SetParameter(TxParameterID::CreateTime, getTimestamp());

            sender.m_Wallet.StartTransaction(parameters);
        }
        /*auto cursor = node.get_Processor().m_Cursor;
        if (cursor.m_Sid.m_Height == fork1Height + 5)
        {
            auto currentHeight = cursor.m_Sid.m_Height;
            bool isBeamSide = !isBeamOwnerStart;
            auto parameters = InitNewSwap(isBeamOwnerStart ? receiver.m_WalletID : sender.m_WalletID,
                currentHeight, beamAmount, beamFee, wallet::AtomicSwapCoin::Bitcoin, swapAmount, feeRate, isBeamSide);

            if (useSecureIDs)
            {
                parameters.SetParameter(TxParameterID::MySecureWalletID, isBeamOwnerStart ? receiver.m_SecureWalletID : sender.m_SecureWalletID);
            }

            TestWalletRig* initiator = &sender;
            TestWalletRig* acceptor = &receiver;
            if (isBeamOwnerStart)
            {
                std::swap(initiator, acceptor);
            }

            initiator->m_Wallet.StartTransaction(parameters);
            auto acceptParams = AcceptSwapParameters(parameters, acceptor->m_WalletID, beamFee, feeRate);
            if (useSecureIDs)
            {
                acceptParams.SetParameter(TxParameterID::MySecureWalletID, acceptor->m_SecureWalletID);
            }
            txID = acceptor->m_Wallet.StartTransaction(acceptParams);
        }*/
    });

    InitNodeToTest(node, binaryTreasury, &observer, 32125, 200);

    mainReactor->run();
}

int main()
{
    int logLevel = LOG_LEVEL_DEBUG;
    auto logger = beam::Logger::create(logLevel, logLevel);
    Rules::get().FakePoW = true;
    Rules::get().UpdateChecksum();
    Height fork1Height = 10;
    Height fork2Height = 20;
    Rules::get().pForks[1].m_Height = fork1Height;
    Rules::get().pForks[2].m_Height = fork2Height;

    TestSimpleTx();

    assert(g_failureCount == 0);
    return WALLET_CHECK_RESULT;
}