// Copyright 2018-2021 The Beam Team
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

#include "core/treasury.h"
#include "node/node.h"
#include "wallet/core/wallet_db.h"


namespace beam::wallet
{
    ByteBuffer createTreasury(IWalletDB::Ptr db, const AmountList& amounts = { 5, 2, 1, 9 });

    void InitNodeToTest(Node& node
        , const ByteBuffer& binaryTreasury
        , Node::IObserver* observer
        , uint16_t port = 32125
        , uint32_t powSolveTime = 1000
        , const std::string& path = "mytest.db"
        , const std::vector<io::Address>& peers = {}
        , bool miningNode = true);

    class NodeObserver : public Node::IObserver
    {
    public:
        using Test = std::function<void()>;
        NodeObserver(Test test)
            : m_test(test)
        {
        }

        void OnSyncProgress() override
        {
        }

        void OnStateChanged() override
        {
            m_test();
        }

    private:
        Test m_test;
    };
}