// Copyright 2018-2026 The Beam Team
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

#include "wallet/cli/utils.h"
#include "test_helpers.h"     // WALLET_TEST_INIT / WALLET_CHECK / WALLET_CHECK_RESULT
#include <cassert>

WALLET_TEST_INIT

using namespace beam;
using namespace beam::wallet;
using namespace std;

namespace
{
    void TestParseAmount()
    {
        Amount amount = 0;
        string error;

        WALLET_CHECK(ParseAmount("1.5", amount, error) && amount == 150000000);
        WALLET_CHECK(ParseAmount("0.00000001", amount, error) && amount == 1);
        WALLET_CHECK(ParseAmount("10", amount, error) && amount == 10 * Rules::Coin);

        WALLET_CHECK(!ParseAmount("0", amount, error) && !error.empty());
        error.clear();
        WALLET_CHECK(!ParseAmount("-1", amount, error) && !error.empty());
        error.clear();
        WALLET_CHECK(!ParseAmount("abc", amount, error) && !error.empty());
        error.clear();
        WALLET_CHECK(!ParseAmount("100000000000", amount, error) && !error.empty()); // above the per-tx limit
        error.clear();
        WALLET_CHECK(!ParseAmount("0.000000001", amount, error) && !error.empty()); // less than 1 groth
        error.clear();
        WALLET_CHECK(!ParseAmount("nan", amount, error) && !error.empty());
        error.clear();
        WALLET_CHECK(!ParseAmount("inf", amount, error) && !error.empty());
    }

    void TestParseReceiversList()
    {
        vector<ReceiverAmount> r;
        string error;

        WALLET_CHECK(ParseReceiversList("addr1:1.5,addr2:0.25", r, error));
        WALLET_CHECK(r.size() == 2);
        WALLET_CHECK(r[0].m_Address == "addr1" && r[0].m_Amount == 150000000);
        WALLET_CHECK(r[1].m_Address == "addr2" && r[1].m_Amount == 25000000);

        // file style: one per line, comments, blank lines, other separators
        WALLET_CHECK(ParseReceiversList("# payroll\naddr1:1\r\n\n  addr2:2 # bonus\naddr3:3; addr4:4\taddr5:5\n", r, error));
        WALLET_CHECK(r.size() == 5);
        WALLET_CHECK(r[0].m_Address == "addr1" && r[0].m_Amount == Rules::Coin);
        WALLET_CHECK(r[1].m_Address == "addr2" && r[1].m_Amount == 2 * Rules::Coin);
        WALLET_CHECK(r[4].m_Address == "addr5" && r[4].m_Amount == 5 * Rules::Coin);

        // single receiver is fine
        WALLET_CHECK(ParseReceiversList("addr1:7", r, error) && r.size() == 1);

        // failures leave a message
        auto fails = [&](const char* s)
        {
            error.clear();
            bool res = ParseReceiversList(s, r, error);
            return !res && !error.empty();
        };

        WALLET_CHECK(fails(""));
        WALLET_CHECK(fails(" , ;\n# only a comment\n"));
        WALLET_CHECK(fails("addr1"));                  // no amount
        WALLET_CHECK(fails("addr1:"));                 // empty amount
        WALLET_CHECK(fails(":1"));                     // empty address
        WALLET_CHECK(fails("addr1:1,addr2"));          // second entry malformed
        WALLET_CHECK(fails("addr1:0"));                // zero
        WALLET_CHECK(fails("addr1:-1"));               // negative
        WALLET_CHECK(fails("addr1:1x"));               // garbage
        WALLET_CHECK(fails("addr1:1,5"));              // decimal comma splits into a malformed entry
        WALLET_CHECK(fails("addr1:1,addr2:2,addr1:3")); // duplicate receiver
        WALLET_CHECK(error.find("addr1") != string::npos);
    }
}

thread_local const beam::Rules* beam::Rules::s_pInstance = nullptr;

int main()
{
    beam::Rules r;
    beam::Rules::Scope scopeRules(r);

    TestParseAmount();
    TestParseReceiversList();

    return WALLET_CHECK_RESULT;
}
