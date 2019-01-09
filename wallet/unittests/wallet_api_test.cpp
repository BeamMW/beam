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

#include <iostream>

#include "test_helpers.h"

#include "wallet/api.h"
#include "nlohmann/json.hpp"

using namespace std;
using namespace beam;
using json = nlohmann::json;

WALLET_TEST_INIT

#define JSON_CODE(...) #__VA_ARGS__
#define CHECK_JSON_FIELD(msg, name) WALLET_CHECK(msg.find(name) != msg.end())

using jsonFunc = std::function<void(const json&)>;

namespace
{
    void testErrorHeader(const json& msg)
    {
        CHECK_JSON_FIELD(msg, "jsonrpc");
        CHECK_JSON_FIELD(msg, "id");
        CHECK_JSON_FIELD(msg, "error");
        CHECK_JSON_FIELD(msg["error"], "code");
        CHECK_JSON_FIELD(msg["error"], "message");

        WALLET_CHECK(msg["jsonrpc"] == "2.0");
    }

    void testMethodHeader(const json& msg)
    {
        CHECK_JSON_FIELD(msg, "jsonrpc");
        CHECK_JSON_FIELD(msg, "id");
        CHECK_JSON_FIELD(msg, "method");

        WALLET_CHECK(msg["jsonrpc"] == "2.0");
        WALLET_CHECK(msg["id"] > 0);
        WALLET_CHECK(msg["method"].is_string());
    }

    void testResultHeader(const json& msg)
    {
        CHECK_JSON_FIELD(msg, "jsonrpc");
        CHECK_JSON_FIELD(msg, "id");
        CHECK_JSON_FIELD(msg, "result");

        WALLET_CHECK(msg["jsonrpc"] == "2.0");
        WALLET_CHECK(msg["id"] > 0);
    }

    class WalletApiHandlerBase : public IWalletApiHandler
    {
        void onInvalidJsonRpc(const json& msg) override {}
        
#define MESSAGE_FUNC(strct, name) virtual void onMessage(int id, const strct& data) override {};
        WALLET_API_METHODS(MESSAGE_FUNC)
#undef MESSAGE_FUNC
    };

    void testInvalidJsonRpc(jsonFunc func, const std::string& msg)
    {
        class WalletApiHandler : public WalletApiHandlerBase
        {
        public:
            jsonFunc func;

            void onInvalidJsonRpc(const json& msg) override
            {
                cout << msg << endl;
                func(msg);
            }
        };

        WalletApiHandler handler;
        handler.func = func;

        WalletApi api(handler);

        WALLET_CHECK(api.parse(msg.data(), msg.size()));
    }

    void testCreateAddressJsonRpc(const std::string& msg)
    {
        class WalletApiHandler : public WalletApiHandlerBase
        {
        public:

            void onInvalidJsonRpc(const json& msg) override
            {
                WALLET_CHECK(!"invalid create_address api json!!!");

                cout << msg["error"]["message"] << endl;
            }

            void onMessage(int id, const CreateAddress& data) override 
            {
                WALLET_CHECK(id > 0);
                WALLET_CHECK(data.metadata == "<meta>custom user data</meta>");
            }
        };

        WalletApiHandler handler;
        WalletApi api(handler);

        WALLET_CHECK(api.parse(msg.data(), msg.size()));

        {
            std::string addr = "472e17b0419055ffee3b3813b98ae671579b0ac0dcd6f1a23b11a75ab148cc67";
            WalletID walletID;
            walletID.FromHex(addr);

            WALLET_CHECK(walletID.IsValid());

            json res;
            CreateAddress::Response response{ walletID };
            api.getResponse(123, response, res);
            testResultHeader(res);

            cout << res["result"] << endl;

            WALLET_CHECK(res["id"] == 123);

            WalletID walletID2;
            walletID2.FromHex(res["result"]);
            WALLET_CHECK(walletID.cmp(walletID2) == 0);
        }
    }

    void testBalanceJsonRpc(const std::string& msg)
    {
        class WalletApiHandler : public WalletApiHandlerBase
        {
        public:

            void onInvalidJsonRpc(const json& msg) override
            {
                WALLET_CHECK(!"invalid balance api json!!!");

                cout << msg["error"]["message"] << endl;
            }

            void onMessage(int id, const Balance& data) override
            {
                WALLET_CHECK(id > 0);
            }
        };

        WalletApiHandler handler;
        WalletApi api(handler);

        WALLET_CHECK(api.parse(msg.data(), msg.size()));

        {
            json res;
            Balance::Response balance{123, 456, 789};
            api.getResponse(123, balance, res);
            testResultHeader(res);

            WALLET_CHECK(res["id"] == 123);
            WALLET_CHECK(res["result"] != nullptr);
            WALLET_CHECK(res["result"]["available"] == 123);
            WALLET_CHECK(res["result"]["in_progress"] == 456);
            WALLET_CHECK(res["result"]["locked"] == 789);
        }
    }

    void testGetUtxoJsonRpc(const std::string& msg)
    {
        class WalletApiHandler : public WalletApiHandlerBase
        {
        public:

            void onInvalidJsonRpc(const json& msg) override
            {
                WALLET_CHECK(!"invalid get_utxo api json!!!");

                cout << msg["error"]["message"] << endl;
            }

            void onMessage(int id, const GetUtxo& data) override
            {
                WALLET_CHECK(id > 0);
            }
        };

        WalletApiHandler handler;
        WalletApi api(handler);

        WALLET_CHECK(api.parse(msg.data(), msg.size()));

        {
            json res;
            GetUtxo::Response getUtxo;

            const int Count = 10;
            for(int i = 0; i < Count; i++)
            {
                Coin coin{ Amount(1234+i) };
                coin.m_ID.m_Type = Key::Type::Regular;
                coin.m_ID.m_Idx = 132+i;
                coin.m_createHeight = 1000;
                coin.m_maturity = 60;
                getUtxo.utxos.push_back(coin);
            }

            api.getResponse(123, getUtxo, res);
            testResultHeader(res);

            WALLET_CHECK(res["id"] == 123);
            auto& result = res["result"];
            WALLET_CHECK(result != nullptr);
            WALLET_CHECK(result.size() == Count);

            for (int i = 0; i < Count; i++)
            {
                WALLET_CHECK(result[i]["id"] == 132 + i);
                WALLET_CHECK(result[i]["amount"] == 1234 + i);
                WALLET_CHECK(result[i]["type"] == "norm");
                WALLET_CHECK(result[i]["height"] == 1000);
                WALLET_CHECK(result[i]["maturity"] == 60);
            }
        }
    }

    void testSendJsonRpc(const std::string& msg)
    {
        class WalletApiHandler : public WalletApiHandlerBase
        {
        public:

            void onInvalidJsonRpc(const json& msg) override
            {
                WALLET_CHECK(!"invalid send api json!!!");

                cout << msg["error"]["message"] << endl;
            }

            void onMessage(int id, const Send& data) override
            {
                WALLET_CHECK(id > 0);

                WALLET_CHECK(data.session == 15);
                WALLET_CHECK(data.value == 12342342);
                WALLET_CHECK(to_string(data.address) == "472e17b0419055ffee3b3813b98ae671579b0ac0dcd6f1a23b11a75ab148cc67");
            }
        };

        WalletApiHandler handler;
        WalletApi api(handler);

        WALLET_CHECK(api.parse(msg.data(), msg.size()));

        {
            json res;
            Send::Response send;

            api.getResponse(123, send, res);
            testResultHeader(res);

            WALLET_CHECK(res["id"] == 123);
            WALLET_CHECK(res["result"]["txId"] > 0);
        }
    }

    void testStatusJsonRpc(const std::string& msg)
    {
        class WalletApiHandler : public WalletApiHandlerBase
        {
        public:

            void onInvalidJsonRpc(const json& msg) override
            {
                WALLET_CHECK(!"invalid status api json!!!");

                cout << msg["error"]["message"] << endl;
            }

            void onMessage(int id, const Status& data) override
            {
                WALLET_CHECK(id > 0);
                WALLET_CHECK(to_hex(data.txId.data(), data.txId.size()) == "10c4b760c842433cb58339a0fafef3db");
            }
        };

        WalletApiHandler handler;
        WalletApi api(handler);

        WALLET_CHECK(api.parse(msg.data(), msg.size()));

        {
            json res;
            Status::Response status;

            api.getResponse(123, status, res);
            testResultHeader(res);

            WALLET_CHECK(res["id"] == 123);
        }
    }

    void testSplitJsonRpc(const std::string& msg)
    {
        class WalletApiHandler : public WalletApiHandlerBase
        {
        public:

            void onInvalidJsonRpc(const json& msg) override
            {
                WALLET_CHECK(!"invalid split api json!!!");

                cout << msg["error"]["message"] << endl;
            }

            void onMessage(int id, const Split& data) override
            {
                WALLET_CHECK(id > 0);

                WALLET_CHECK(data.session == 123);
                WALLET_CHECK(data.coins[0] == 11);
                WALLET_CHECK(data.coins[1] == 12);
                WALLET_CHECK(data.coins[2] == 13);
                WALLET_CHECK(data.coins[3] == 50000000000000);
                WALLET_CHECK(data.fee == 4);
            }
        };

        WalletApiHandler handler;
        WalletApi api(handler);

        WALLET_CHECK(api.parse(msg.data(), msg.size()));

        {
            json res;
            Split::Response split;

            api.getResponse(123, split, res);
            testResultHeader(res);

            WALLET_CHECK(res["id"] == 123);
            WALLET_CHECK(res["result"]["txId"] > 0);
        }
    }
}

int main()
{
    testInvalidJsonRpc([](const json& msg)
    {
        testErrorHeader(msg);

        WALLET_CHECK(msg["id"] == nullptr);
        WALLET_CHECK(msg["error"]["code"] == INVALID_JSON_RPC);
    }, JSON_CODE({}));

    testInvalidJsonRpc([](const json& msg)
    {
        testErrorHeader(msg);

        WALLET_CHECK(msg["id"] == nullptr);
        WALLET_CHECK(msg["error"]["code"] == INVALID_JSON_RPC);
    }, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "method" : 1,
        "params" : "bar"
    }));

    testInvalidJsonRpc([](const json& msg)
    {
        testErrorHeader(msg);

        WALLET_CHECK(msg["id"] == 123);
        WALLET_CHECK(msg["error"]["code"] == NOTFOUND_JSON_RPC);
    }, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 123,
        "method" : "balance123",
        "params" : "bar"
    }));

    testBalanceJsonRpc(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "balance"
    }));

    testCreateAddressJsonRpc(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "create_address",
        "params" :
        {
            "lifetime" : 24,
            "metadata" : "<meta>custom user data</meta>"
        }
    }));

    testInvalidJsonRpc([](const json& msg)
    {
        testErrorHeader(msg);

        WALLET_CHECK(msg["id"] == 12345);
        WALLET_CHECK(msg["error"]["code"] == INVALID_JSON_RPC);
    }, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "create_address",
        "params" :
        {
            "metadata" : "<meta>custom user data</meta>"
        }
    }));

    testGetUtxoJsonRpc(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "get_utxo"
    }));

    testSendJsonRpc(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "send",
        "params" : 
        {
            "session" : 15,
            "value" : 12342342,
            "address" : "472e17b0419055ffee3b3813b98ae671579b0ac0dcd6f1a23b11a75ab148cc67"
        }
    }));

    testStatusJsonRpc(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "tx_status",
        "params" :
        {
            "txId" : "10c4b760c842433cb58339a0fafef3db"
        }
    }));

    testSplitJsonRpc(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "split",
        "params" :
        {
            "session" : 123,
            "coins" : [11, 12, 13, 50000000000000],
            "fee" : 4
        }
    }));

    return WALLET_CHECK_RESULT;
}
