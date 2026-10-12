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
#include <core/block_crypt.h>
#include "test_helpers.h"
#include "wallet/api/i_wallet_api.h"
#include "wallet/api/v6_0/v6_api.h"
#include "utility/logger.h"
#include "nlohmann/json.hpp"
#include "wallet/api/i_swaps_provider.h"
#include "wallet/core/strings_resources.h"
#include "utility/io/reactor.h"
#include <boost/filesystem.hpp>
#include <set>

using namespace std;
using namespace beam;
using namespace beam::wallet;

WALLET_TEST_INIT

#define JSON_CODE(...) #__VA_ARGS__
#define CHECK_JSON_FIELD(msg, name) WALLET_CHECK(msg.find(name) != msg.end())
#define CHECK_JSON_FIELD_ABSENT(msg, name) WALLET_CHECK(msg.find(name) == msg.end())

using jsonFunc = std::function<void(const json&)>;

namespace
{
    void testErrorHeader(const json& msg)
    {
        CHECK_JSON_FIELD(msg, "jsonrpc");
        CHECK_JSON_FIELD(msg, "error");
        CHECK_JSON_FIELD(msg["error"], "code");
        CHECK_JSON_FIELD(msg["error"], "message");

        WALLET_CHECK(msg["jsonrpc"] == "2.0");
    }

    void testErrorHeaderWithId(const json& msg)
    {
        testErrorHeader(msg);
        CHECK_JSON_FIELD(msg, "id");
    }

    void testResultHeader(const json& msg)
    {
        CHECK_JSON_FIELD(msg, "jsonrpc");
        CHECK_JSON_FIELD(msg, "id");
        CHECK_JSON_FIELD(msg, "result");

        WALLET_CHECK(msg["jsonrpc"] == "2.0");
        WALLET_CHECK(msg["id"] > 0);
    }

    enum Fork
    {
        NoFork,
        Fork1,
        Fork2,
        Fork3,
    };

    class WalletApiTest
        : public wallet::V6Api
        , IWalletApiHandler
    {
    public:
        WalletApiTest(Fork fork, const ApiInitData& initData)
            : V6Api(*this, initData)
        {
            switch(fork) {
                case Fork::Fork1: _currentHeight = Rules::get().pForks[1].m_Height; break;
                case Fork::Fork2: _currentHeight = Rules::get().pForks[2].m_Height; break;
                case Fork::Fork3: _currentHeight = Rules::get().pForks[3].m_Height; break;
                default: _currentHeight = Rules::get().pForks[1].m_Height - 1; break;
            }
        }

        #define MESSAGE_FUNC(strct, name, ...) void onHandle##strct(const JsonRpcId& id, strct&& data) override { \
                WALLET_CHECK(!"error, onHandle should be never called"); };

        V6_API_METHODS(MESSAGE_FUNC)
        #undef MESSAGE_FUNC

        void sendAPIResponse(const json& resp) override
        {
            if (resp["error"].empty())
            {
                onAPISuccess(resp);
            }
            else
            {
                onAPIError(resp);
            }
        }

        void onParseError(const json& msg) override
        {
            onAPIError(msg);
        }

        virtual void onAPISuccess(const json&)
        {
            assert(false);
            WALLET_CHECK(!"invalid api test - success");
        }

        virtual void onAPIError(const json&)
        {
            assert(false);
            WALLET_CHECK(!"invalid api test - error");
        }

        Height get_TipHeight() const override
        {
            return _currentHeight;
        }

    private:
        Height _currentHeight;
    };

    void testInvalidJsonRpc(Fork fork, jsonFunc func, const std::string& msg)
    {
        class ApiTest : public WalletApiTest
        {
        public:
            void onAPIError(const json& msg) override
            {
                cout << msg << endl;
                _func(msg);
            }

            explicit ApiTest(Fork fork, jsonFunc func)
                : WalletApiTest(fork, ApiInitData())
                , _func(std::move(func))
            {}

        private:
            jsonFunc _func;
        };

        ApiTest api(fork, std::move(func));
        WALLET_CHECK(ApiSyncMode::DoneSync == api.executeAPIRequest(msg.data(), msg.size()));
    }

    void testInvalidJsonRpc(Fork fork, ApiError code, const std::string& msg)
    {
        class ApiTest : public WalletApiTest
        {
        public:
            void onAPIError(const json& msg) override
            {
                cout << msg << endl;
                testErrorHeader(msg);

                ApiError code = msg["error"]["code"];
                WALLET_CHECK(code == _code);
            }

            explicit ApiTest(Fork fork, ApiError code)
                : WalletApiTest(fork, ApiInitData())
                , _code(code)
            {}

        private:
            ApiError _code;
        };

        ApiTest api(fork, code);
        WALLET_CHECK(ApiSyncMode::DoneSync == api.executeAPIRequest(msg.data(), msg.size()));
    }

    void testAppsApi()
    {
        class ApiTest : public WalletApiTest
        {
        public:
            explicit ApiTest(const ApiInitData& data)
                : WalletApiTest(Fork3, data)
            {}
        };

        auto testNotAllowed = [] (const std::string& json) {
            ApiInitData appApiData;
            appApiData.appName = "appname";
            appApiData.appId   = "appid";
            ApiTest parse(appApiData);

            auto pres =  parse.parseAPIRequest(json.data(), json.size());
            WALLET_CHECK(pres.is_initialized());
            WALLET_CHECK(!pres->acinfo.appsAllowed);
        };

        testNotAllowed(JSON_CODE(
        {
            "jsonrpc": "2.0",
            "id"     : 12345,
            "method" : "change_password",
            "params" : {
                "new_pass": "abra cadabra"
            }
        }));

        testNotAllowed(JSON_CODE(
        {
            "jsonrpc": "2.0",
            "id" : 12345,
            "method" : "tx_asset_issue",
            "params" :
            {
                "value": 6,
                "asset_id": 1
            }
        }));

        testNotAllowed(JSON_CODE(
        {
            "jsonrpc": "2.0",
            "id" : 12345,
            "method" : "tx_asset_consume",
            "params" :
            {
                "value": 6,
                "asset_id": 1
            }
        }));

        testNotAllowed(JSON_CODE(
        {
            "jsonrpc": "2.0",
            "id" : 12345,
            "method" : "tx_split",
            "params" :
            {
                "coins" : [11, 12, 13, 500],
                "asset_id": 1
            }
        }));

        testNotAllowed(JSON_CODE(
        {
            "jsonrpc": "2.0",
            "id" : 12345,
            "method" : "get_utxo",
            "params":
            {
                "filter":
                {
                    "asset_id": 1
                }
            }
        }));

        testNotAllowed(JSON_CODE(
        {
            "jsonrpc": "2.0",
            "id"     : 12345,
            "method" : "wallet_status",
            "params" : {}
        }));

        testNotAllowed(JSON_CODE(
        {
            "jsonrpc": "2.0",
            "id"     : 12345,
            "method" : "set_confirmations_count",
            "params" : {
                "count": 100
            }
        }));

        testNotAllowed(JSON_CODE(
        {
            "jsonrpc": "2.0",
            "id"     : 12345,
            "method" : "swap_offers_list",
            "params" : {}
        }));

        testNotAllowed(JSON_CODE(
        {
            "jsonrpc": "2.0",
            "id"     : 12345,
            "method" : "swap_offers_board",
            "params" : {}
        }));

        /*
        testNotAllowed(JSON_CODE(
        {
            "jsonrpc": "2.0",
            "id"     : 12345,
            "method" : "swap_create_offer",
            "params" : {
            }
        }));

        testNotAllowed(JSON_CODE(
        {
            "jsonrpc": "2.0",
            "id"     : 12345,
            "method" : "swap_offer_status",
            "params" : {}
        }));

        testNotAllowed(JSON_CODE(
        {
            "jsonrpc": "2.0",
            "id"     : 12345,
            "method" : "swap_decode_token",
            "params" : {}
        }));

        testNotAllowed(JSON_CODE(
        {
            "jsonrpc": "2.0",
            "id"     : 12345,
            "method" : "swap_publish_offer",
            "params" : {}
        }));

        testNotAllowed(JSON_CODE(
        {
            "jsonrpc": "2.0",
            "id"     : 12345,
            "method" : "swap_accept_offer",
            "params" : {}
        }));

        testNotAllowed(JSON_CODE(
        {
            "jsonrpc": "2.0",
            "id"     : 12345,
            "method" : "swap_cancel_offer",
            "params" : {}
        }));

        testNotAllowed(JSON_CODE(
        {
            "jsonrpc": "2.0",
            "id"     : 12345,
            "method" : "swap_get_balance",
            "params" : {}
        }));

        testNotAllowed(JSON_CODE(
        {
            "jsonrpc": "2.0",
            "id"     : 12345,
            "method" : "swap_recommended_fee_rate",
            "params" : {}
        }));
        */
    }

    void testCreateAddressJsonRpc(const std::string& msg)
    {
        class ApiTest : public WalletApiTest
        {
        public:
            void onHandleCreateAddress(const JsonRpcId& id, CreateAddress&& data) override
            {
                WALLET_CHECK(id > 0);
            }
            ApiTest(): WalletApiTest(Fork::NoFork, ApiInitData()) {}
        };

        ApiTest api;
        WALLET_CHECK(ApiSyncMode::DoneSync == api.executeAPIRequest(msg.data(), msg.size()));

        {
            std::string addr = "472e17b0419055ffee3b3813b98ae671579b0ac0dcd6f1a23b11a75ab148cc67";
            WalletID walletID;
            walletID.FromHex(addr);

            WALLET_CHECK(walletID.IsValid());

            json res;
            CreateAddress::Response response{ std::to_string(walletID) };
            api.getResponse(123, response, res);
            testResultHeader(res);

            cout << res["result"] << endl;

            WALLET_CHECK(res["id"] == 123);

            WalletID walletID2;
            walletID2.FromHex(res["result"]);
            WALLET_CHECK(walletID.cmp(walletID2) == 0);
        }
    }

    void testDefaultCreateAddressJsonRpc(const std::string& msg)
    {
        class ApiTest : public WalletApiTest
        {
        public:
            ApiTest(): WalletApiTest(Fork::NoFork, ApiInitData()) {}
            void onHandleCreateAddress(const JsonRpcId& id, CreateAddress&& data) override
            {
                WALLET_CHECK(id > 0);
                WALLET_CHECK(data.type == TokenType::RegularOldStyle);
                WALLET_CHECK(data.offlinePayments == 1);
            }
        };

        ApiTest api;
        WALLET_CHECK(ApiSyncMode::DoneSync == api.executeAPIRequest(msg.data(), msg.size()));
    }

    void testGetUtxoJsonRpc(Fork fork, const std::string& msg)
    {
        class ApiTest : public WalletApiTest
        {
        public:
            void onAPIError(const json& msg) override
            {
                WALLET_CHECK(!"invalid get_utxo api json!!!");
                cout << msg["error"] << endl;
            }

            void onHandleGetUtxo(const JsonRpcId& id, GetUtxo&& data) override
            {
                WALLET_CHECK(id > 0);
                WALLET_CHECK(data.filter.assetId && *data.filter.assetId == 1);
            }

            explicit ApiTest(Fork fork): WalletApiTest(fork, ApiInitData()) {}
        };

        ApiTest api(fork);
        WALLET_CHECK(ApiSyncMode::DoneSync == api.executeAPIRequest(msg.data(), msg.size()));

        {
            json res;
            GetUtxo::Response getUtxo;

            const int Count = 10;
            for(int i = 0; i < Count; i++)
            {
                Coin coin{ Amount(1234+i) };
                coin.m_ID.m_Type = Key::Type::Regular;
                coin.m_ID.m_Idx = 132+i;
                coin.m_maturity = 60;
				coin.m_confirmHeight = 60;
				coin.m_status = Coin::Status::Available; // maturity is returned only for confirmed coins
				ApiCoin::EmplaceCoin(getUtxo.coins, coin);
            }

            api.getResponse(123, getUtxo, res);
            testResultHeader(res);

            WALLET_CHECK(res["id"] == 123);
            auto& result = res["result"];
            WALLET_CHECK(result != nullptr);
            WALLET_CHECK(result.size() == Count);

            for (int i = 0; i < Count; i++)
            {                
                WALLET_CHECK(Coin::FromString(result[i]["id"])->m_Idx == uint64_t(132 + i));
                WALLET_CHECK(result[i]["amount"] == 1234 + i);
                WALLET_CHECK(result[i]["type"] == "norm");
                WALLET_CHECK(result[i]["maturity"] == 60);
            }
        }
    }

    void testSendJsonRpc(Fork fork, const std::string& msg)
    {
        class ApiTest : public WalletApiTest
        {
        public:
            explicit ApiTest(Fork fork): WalletApiTest(fork, ApiInitData()) {}

            void onAPIError(const json& msg) override
            {
                WALLET_CHECK(!"invalid send api json!!!");
                cout << msg["error"] << endl;
            }

            void onHandleSend(const JsonRpcId& id, Send&& data) override
            {
                WALLET_CHECK(id > 0);
                WALLET_CHECK(data.value == 12342342);
                WALLET_CHECK(data.tokenTo == "472e17b0419055ffee3b3813b98ae671579b0ac0dcd6f1a23b11a75ab148cc67");
                WALLET_CHECK(data.assetId && *data.assetId == 1);

                if(data.tokenFrom)
                {
                    WALLET_CHECK(*data.tokenFrom == "19d0adff5f02787819d8df43b442a49b43e72a8b0d04a7cf995237a0422d2be83b6");
                }
            }
        };

        ApiTest api(fork);
        WALLET_CHECK(ApiSyncMode::DoneSync == api.executeAPIRequest(msg.data(), msg.size()));

        {
            json res;
            Send::Response send = {};

            api.getResponse(123, send, res);
            testResultHeader(res);

            WALLET_CHECK(res["id"] == 123);
            WALLET_CHECK(res["result"]["txId"] > 0);
        }
    }

    template<typename T>
    void testICJsonRpc(const std::string& msg)
    {
        class ApiTest : public WalletApiTest
        {
        public:
            ApiTest(): WalletApiTest(Fork2, ApiInitData()) {}

            void onAPIError(const json& msg) override
            {
                WALLET_CHECK(!"invalid issue/consume api json!!!");
                cout << msg["error"] << endl;
            }

            void onHandleIssue(const JsonRpcId& id, Issue&& data) override
            {
                WALLET_CHECK(id > 0);
                WALLET_CHECK(data.assetId > 0);
                WALLET_CHECK(data.value > 0);
            }

            void onHandleConsume(const JsonRpcId& id, Consume&& data) override
            {
                WALLET_CHECK(id > 0);
                WALLET_CHECK(data.assetId > 0);
                WALLET_CHECK(data.value > 0);
            }
        };

        ApiTest api;
        WALLET_CHECK(ApiSyncMode::DoneSync == api.executeAPIRequest(msg.data(), msg.size()));

        {
            json res;
            typename T::Response status = {};
            status.txId = { 1,2,3 };
            api.getResponse(12345, status, res);
            testResultHeader(res);
            WALLET_CHECK(res["id"] == 12345);
        }
    }

    void testAIJsonRpc(const std::string& msg)
    {
        class ApiTest : public WalletApiTest
        {
        public:
            void onAPIError(const json& msg) override
            {
                WALLET_CHECK(!"invalid asset info api json!!!");
                cout << msg["error"] << endl;
            }

            void onHandleTxAssetInfo(const JsonRpcId& id, TxAssetInfo&& data) override
            {
                WALLET_CHECK(id > 0);
                WALLET_CHECK(data.assetId);
            }

            ApiTest(): WalletApiTest(Fork2, ApiInitData()) {}
        };

        ApiTest api;
        WALLET_CHECK(ApiSyncMode::DoneSync == api.executeAPIRequest(msg.data(), msg.size()));

        {
            json res;
            typename TxAssetInfo::Response status = {};
            status.txId = { 3,1,3 };
            api.getResponse(12345, status, res);
            testResultHeader(res);

            WALLET_CHECK(res["id"] == 12345);
        }
    }

    void testGetAssetInfoJsonRpc(const std::string& msg)
    {
        class ApiTest : public WalletApiTest
        {
        public:
            void onAPIError(const json& msg) override
            {
                WALLET_CHECK(!"invalid GetAssetInfo api json!!!");
                cout << msg["error"] << endl;
            }

            void onHandleGetAssetInfo(const JsonRpcId& id, GetAssetInfo&& data) override
            {
                WALLET_CHECK(id > 0);
                WALLET_CHECK(data.assetId > 0);
            }

            ApiTest(): WalletApiTest(Fork2, ApiInitData()) {}
        };

        ApiTest api;
        WALLET_CHECK(ApiSyncMode::DoneSync == api.executeAPIRequest(msg.data(), msg.size()));

        {
            json res;
            GetAssetInfo::Response status;

            api.getResponse(12345, status, res);
            testResultHeader(res);

            WALLET_CHECK(res["id"] == 12345);
        }
    }

    void testStatusJsonRpc(const std::string& msg)
    {
        class ApiTest : public WalletApiTest
        {
        public:
            void onAPIError(const json& msg) override
            {
                WALLET_CHECK(!"invalid status api json!!!");
                cout << msg["error"] << endl;
            }

            void onHandleStatus(const JsonRpcId& id, Status&& data) override
            {
                WALLET_CHECK(id > 0);
                WALLET_CHECK(to_hex(data.txId.data(), data.txId.size()) == "10c4b760c842433cb58339a0fafef3db");
            }

            ApiTest(): WalletApiTest(NoFork, ApiInitData()) {}
        };

        ApiTest api;
        WALLET_CHECK(ApiSyncMode::DoneSync == api.executeAPIRequest(msg.data(), msg.size()));

        {
            json res;
            Status::Response status;

            api.getResponse(123, status, res);
            testResultHeader(res);

            WALLET_CHECK(res["id"] == 123);
        }
    }

    void testSplitJsonRpc(Fork fork, const std::string& msg)
    {
        class ApiTest : public WalletApiTest
        {
        public:
            void onAPIError(const json& msg) override
            {
                cout << msg["error"] << endl;
                WALLET_CHECK(!"invalid split api json!!!");
            }

            void onHandleSplit(const JsonRpcId& id, Split&& data) override
            {
                WALLET_CHECK(id > 0);

                WALLET_CHECK(data.coins[0] == 11);
                WALLET_CHECK(data.coins[1] == 12);
                WALLET_CHECK(data.coins[2] == 13);
                WALLET_CHECK(data.coins[3] == 50000000000000);
                WALLET_CHECK(data.fee == 100);
                WALLET_CHECK(data.assetId && *data.assetId == 1);
            }

            explicit ApiTest(Fork fork): WalletApiTest(fork, ApiInitData()) {}
        };

        ApiTest api(fork);
        WALLET_CHECK(ApiSyncMode::DoneSync == api.executeAPIRequest(msg.data(), msg.size()));

        {
            json res;
            Split::Response split = {};

            api.getResponse(123, split, res);
            testResultHeader(res);

            WALLET_CHECK(res["id"] == 123);
            WALLET_CHECK(res["result"]["txId"] > 0);
        }
    }

    void testTxListJsonRpc(Fork fork, const std::string& msg)
    {
        class ApiTest : public WalletApiTest
        {
        public:
            void onAPIError(const json& msg) override
            {
                WALLET_CHECK(!"invalid list api json!!!");
                cout << msg["error"] << endl;
            }

            void onHandleTxList(const JsonRpcId& id, TxList&& data) override
            {
                WALLET_CHECK(id > 0);
                WALLET_CHECK(*data.filter.status == TxStatus::Completed);
                WALLET_CHECK(data.filter.assetId && *data.filter.assetId == 1);
            }

            explicit ApiTest(Fork fork): WalletApiTest(fork, ApiInitData()) {}
        };

        ApiTest api(fork);
        WALLET_CHECK(ApiSyncMode::DoneSync == api.executeAPIRequest(msg.data(), msg.size()));

        {
            json res;
            TxList::Response txList;

            api.getResponse(123, txList, res);
            testResultHeader(res);

            WALLET_CHECK(res["id"] == 123);
        }
    }

    void testTxListPaginationJsonRpc(const std::string& msg)
    {
        class ApiTest : public WalletApiTest
        {
        public:
            void onAPIError(const json& msg) override
            {
                WALLET_CHECK(!"invalid list api json!!!");
                cout << msg["error"] << endl;
            }

            void onHandleTxList(const JsonRpcId& id, TxList&& data) override
            {
                WALLET_CHECK(id > 0);

                WALLET_CHECK(data.skip == 10);
                WALLET_CHECK(data.count == 10);
            }

            ApiTest(): WalletApiTest(NoFork, ApiInitData()) {}
        };

        ApiTest api;
        WALLET_CHECK(ApiSyncMode::DoneSync == api.executeAPIRequest(msg.data(), msg.size()));
    }

    void testValidateAddressJsonRpc(const std::string& msg, bool valid)
    {
        class ApiTest : public WalletApiTest
        {
        public:
            explicit ApiTest(bool valid_)
                : WalletApiTest(NoFork, ApiInitData())
                , _valid(valid_)
            {}

            void onAPIError(const json& msg) override
            {
                WALLET_CHECK(!"invalid validate_address api json!!!");
                cout << msg["error"] << endl;
            }

            void onHandleValidateAddress(const JsonRpcId& id, ValidateAddress&& data) override
            {
                WALLET_CHECK(id > 0);
                WALLET_CHECK(CheckReceiverAddress(data.token) == _valid);
            }

        private:
            bool _valid;
        };

        ApiTest api(valid);
        WALLET_CHECK(ApiSyncMode::DoneSync == api.executeAPIRequest(msg.data(), msg.size()));

        {
            json res;
            ValidateAddress::Response validateResponce;

            validateResponce.isMine = true;
            validateResponce.isValid = valid;
            validateResponce.type = TokenType::Offline;
            validateResponce.payments = 12;

            api.getResponse(123, validateResponce, res);
            testResultHeader(res);

            WALLET_CHECK(res["id"] == 123);
            WALLET_CHECK(res["result"]["is_mine"] == true);
            WALLET_CHECK(res["result"]["is_valid"] == valid);
            WALLET_CHECK(res["result"]["type"] == "offline");
            WALLET_CHECK(res["result"]["payments"] == 12);
        }
    }

    void testGenerateTxIdJsonRpc(const std::string& msg)
    {
        class ApiTest : public WalletApiTest
        {
        public:
            void onAPIError(const json& msg) override
            {
                WALLET_CHECK(!"invalid list api json!!!");
                cout << msg["error"] << endl;
            }

            void onHandleGenerateTxId(const JsonRpcId& id, GenerateTxId&& data) override
            {
                WALLET_CHECK(id > 0);
            }

            ApiTest(): WalletApiTest(NoFork, ApiInitData()) {}
        };

        ApiTest api;
        WALLET_CHECK(ApiSyncMode::DoneSync == api.executeAPIRequest(msg.data(), msg.size()));

        {
            json res;
            GenerateTxId::Response response{};

            auto id = "10c4b760c842433cb58339a0fafef3db";
            std::copy_n(from_hex(id).begin(), response.txId.size(), response.txId.begin());

            api.getResponse(123, response, res);
            testResultHeader(res);

            WALLET_CHECK(res["id"] == 123);
            WALLET_CHECK(res["result"] == id);
        }
    }

    void testExportPaymentProofJsonRpc(const std::string& msg)
    {
        class ApiTest : public WalletApiTest
        {
        public:
            void onAPIError(const json& msg) override
            {
                WALLET_CHECK(!"invalid list api json!!!");
                cout << msg["error"] << endl;
            }

            void onHandleExportPaymentProof(const JsonRpcId& id, ExportPaymentProof&& data) override
            {
                WALLET_CHECK(id > 0);
            }

            ApiTest(): WalletApiTest(NoFork, ApiInitData()) {}
        };

        ApiTest api;
        WALLET_CHECK(ApiSyncMode::DoneSync == api.executeAPIRequest(msg.data(), msg.size()));

        {
            json res;
            ExportPaymentProof::Response response{};

            auto proof = "8009f28991ef543253c8b6a2caf15cf99e23fb9c2b4ca30dc463c8ceb354d7979e80ef7d4255dd5e885200648abe5826d8e0ba0157d3e8cf9c42dcc8258b036986e50400371789ee82afc25ee29c9c57bcb1018b725a3a94c0ceb1fa7984ea13de4982553e0d78d925a362982182a971e654857b8e407e7ad2e9cb72b2b8228812f8ec50435351000c94e2c85996e9527d9b0c90a1843205a7ec8f99fa534083e5f1d055d9f53894";
            
            response.paymentProof = from_hex(proof);

            api.getResponse(123, response, res);
            testResultHeader(res);

            WALLET_CHECK(res["id"] == 123);
            WALLET_CHECK(res["result"]["payment_proof"] == proof);
        }
    }

    void testVerifyPaymentProofJsonRpc(const std::string& msg)
    {
        class ApiTest : public WalletApiTest
        {
        public:
            void onAPIError(const json& msg) override
            {
                WALLET_CHECK(!"invalid list api json!!!");
                cout << msg["error"] << endl;
            }

            void onHandleVerifyPaymentProof(const JsonRpcId& id, VerifyPaymentProof&& data) override
            {
                WALLET_CHECK(id > 0);
            }

            ApiTest(): WalletApiTest(NoFork, ApiInitData()) {}
        };

        ApiTest api;
        WALLET_CHECK(ApiSyncMode::DoneSync == api.executeAPIRequest(msg.data(), msg.size()));

        {
            json res;
            VerifyPaymentProof::Response response{};
       
            auto proof = "8009f28991ef543253c8b6a2caf15cf99e23fb9c2b4ca30dc463c8ceb354d7979e80ef7d4255dd5e885200648abe5826d8e0ba0157d3e8cf9c42dcc8258b036986e50400371789ee82afc25ee29c9c57bcb1018b725a3a94c0ceb1fa7984ea13de4982553e0d78d925a362982182a971e654857b8e407e7ad2e9cb72b2b8228812f8ec50435351000c94e2c85996e9527d9b0c90a1843205a7ec8f99fa534083e5f1d055d9f53894";
            response.paymentInfo = storage::PaymentInfo::FromByteBuffer(from_hex(proof));

            api.getResponse(123, response, res);
            testResultHeader(res);
       
            WALLET_CHECK(res["id"] == 123);
            auto& result = res["result"];
            WALLET_CHECK(result["is_valid"] == true);
            WALLET_CHECK(result["sender"] == "9f28991ef543253c8b6a2caf15cf99e23fb9c2b4ca30dc463c8ceb354d7979e");
            WALLET_CHECK(result["receiver"] == "ef7d4255dd5e885200648abe5826d8e0ba0157d3e8cf9c42dcc8258b036986e5");
            WALLET_CHECK(result["amount"] == 2300000000);
            WALLET_CHECK(result["kernel"] == "ee82afc25ee29c9c57bcb1018b725a3a94c0ceb1fa7984ea13de4982553e0d78");
        }
    }

    template<typename T>
    void testJsonRpcIdAsValue(const std::string& msg, const T& value)
    {
        class ApiTest : public WalletApiTest
        {
        public:
            explicit ApiTest(const T& value)
                : WalletApiTest(NoFork, ApiInitData()), _value(value) {}

            void onAPIError(const json& msg) override
            {
                WALLET_CHECK(!"invalid api json!!!");
                cout << msg["error"] << endl;
            }

            void onHandleCreateAddress(const JsonRpcId& id, CreateAddress&& data) override
            {
                WALLET_CHECK(id == _value);
            }
        private:
            const T& _value;
        };

        ApiTest api(value);
        WALLET_CHECK(ApiSyncMode::DoneSync == api.executeAPIRequest(msg.data(), msg.size()));
    }

#ifdef BEAM_ATOMIC_SWAP_SUPPORT
    void testGetBalanceJsonRpc(const std::string& msg)
    {
        class ApiTest : public WalletApiTest
        {
        public:
            void onAPIError(const json& msg) override
            {
                WALLET_CHECK(!"invalid list api json!!!");
                cout << msg["error"] << endl;
            }

            void onHandleGetBalance(const JsonRpcId& id, GetBalance&& data) override
            {
                WALLET_CHECK(id > 0);
                WALLET_CHECK(data.coin == AtomicSwapCoin::Litecoin);
            }

            ApiTest(): WalletApiTest(NoFork, ApiInitData()) {}
        };

        ApiTest api;
        WALLET_CHECK(ApiSyncMode::DoneSync == api.executeAPIRequest(msg.data(), msg.size()));

        {
            json res;
            GetBalance::Response response{};

            response.available = 1000;

            api.getResponse(123, response, res);
            testResultHeader(res);

            WALLET_CHECK(res["id"] == 123);
            auto& result = res["result"];
            WALLET_CHECK(result["available"] == 1000);
        }
    }

    void testDecodeTokenJsonRpc(const std::string& msg)
    {
        const std::string kToken = "6xfNAUemTbmp7KRCRydiGStMZe6oRh59LzS7uk1V4eTrUX1mKcCGY7jdtMtSs4XLt6Ug8jWnepMEZCrqSUw7PeKRDZ8yyVZu1WHXzootpybBjX3nVxxHRSdk4ncBGDh1cssmiJhswZC9PfsaJmRKqXJM3x9tcX7EZn5Vjg8";

        class ApiTest : public WalletApiTest
        {
        public:
            explicit ApiTest(std::string value)
                : WalletApiTest(NoFork, ApiInitData())
                , _value(std::move(value))
            {}

            void onAPIError(const json& msg) override
            {
                WALLET_CHECK(!"invalid list api json!!!");
                cout << msg["error"] << endl;
            }

            void onHandleDecodeToken(const JsonRpcId& id, DecodeToken&& data) override
            {
                WALLET_CHECK(id > 0);
                WALLET_CHECK(data.token == _value);
            }

        private:
            std::string _value;
        };

        ApiTest api(kToken);
        WALLET_CHECK(ApiSyncMode::DoneSync == api.executeAPIRequest(msg.data(), msg.size()));

        {
            json res;
            DecodeToken::Response response{};

            response.isMyOffer = false;
            response.isPublic = true;

            auto txParams = ParseParameters(kToken);

            response.offer = SwapOffer(*txParams);

            api.getResponse(123, response, res);
            testResultHeader(res);

            WALLET_CHECK(res["id"] == 123);
            auto& result = res["result"];
            WALLET_CHECK(result["is_public"] == true);
            WALLET_CHECK(result["height_expired"] == 123428);
            WALLET_CHECK(result["is_my_offer"] == false);
            WALLET_CHECK(result["min_height"] == 123398);
            WALLET_CHECK(result["receive_amount"] == 200000000);
            WALLET_CHECK(result["receive_currency"] == "BEAM");
            WALLET_CHECK(result["send_amount"] == 100000000);
            WALLET_CHECK(result["send_currency"] == "BTC");
            WALLET_CHECK(result["height_expired"] == 123428);
            WALLET_CHECK(result["tx_id"] == "d218356770b34fe4aeab01fb12c6074c");
        }
    }

    void testOfferStatusJsonRpc(const std::string& msg)
    {
        const std::string kTxId = "b35fd69030694009b8bf849140d9319e";

        class ApiTest : public WalletApiTest
        {
        public:
            ApiTest(std::string value)
                : WalletApiTest(NoFork, ApiInitData())
                , _value(std::move(value))
            {}

            void onAPIError(const json& msg) override
            {
                WALLET_CHECK(!"invalid list api json!!!");
                cout << msg["error"] << endl;
            }

            void onHandleOfferStatus(const JsonRpcId& id, OfferStatus&& data) override
            {
                WALLET_CHECK(id > 0);
                WALLET_CHECK(to_hex(data.txId.data(), data.txId.size()) == _value);
            }

        private:
            std::string _value;
        };

        ApiTest api(kTxId);
        WALLET_CHECK(ApiSyncMode::DoneSync == api.executeAPIRequest(msg.data(), msg.size()));

        {
            json res;
            OfferStatus::Response response{};

            auto txParams = ParseParameters("6xfHuWNKr45XLyw1pYcB8hixKoF1g8mPRi9dHXL9jr8kqhcjiqntRXzbWmrsSrRLPecjr5vaWQa27ScTB24XdPs5LqSBb318knzZya7dGvNbkm9B1VRgc9hsaQuPu4nJjiYa9ePCCz7VsDNpoB9JKNSGkbFGG7UJR4GWbZe");
            response.offer = SwapOffer(*txParams);

            api.getResponse(123, response, res);
            testResultHeader(res);

            WALLET_CHECK(res["id"] == 123);
            auto& result = res["result"];
            WALLET_CHECK(result["tx_id"] == kTxId);
            WALLET_CHECK(result["status"] == 0);
            WALLET_CHECK(result["status_string"] == "pending");
        }
    }
#endif  // BEAM_ATOMIC_SWAP_SUPPORT
}

template<typename T>
void TestICTx(const char* method)
{
    const auto exp = [&](std::string str) -> auto {
        const char* what = "METHOD";
        const auto index = str.find(what);
        if (index != std::string::npos) {
            const std::string mname = std::string("\"") + method + "\"";
            str.replace(index, strlen(what), mname);
        }
        return str;
    };

    // Invalid asset id
    testInvalidJsonRpc(Fork2, ApiError::InvalidParamsJsonRpc, exp(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id"     : 12345,
        "method" : METHOD,
        "params" :
        {
            "asset_id": -1,
            "value": 10
        }
    })));

    // Invalid meta
    testInvalidJsonRpc(Fork2, ApiError::InvalidParamsJsonRpc, exp(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id"     : 12345,
        "method" : METHOD,
        "params" :
        {
            "asset_meta": "",
            "value": 10
        }
    })));

    // missing asset id & meta
    testInvalidJsonRpc(Fork2, ApiError::InvalidParamsJsonRpc, exp(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id"     : 12345,
        "method" : METHOD,
        "params" :
        {
            "value": 10
        }
    })));

    // Invalid negative value (amount)
    testInvalidJsonRpc(Fork2, ApiError::InvalidParamsJsonRpc, exp(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : METHOD,
        "params" :
        {
            "asset_id": 1,
            "value": -1
        }
    })));

    // Invalid zero value (amount)
    testInvalidJsonRpc(Fork2, ApiError::InvalidParamsJsonRpc, exp(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : METHOD,
        "params" :
        {
            "asset_id": 1,
            "value": 0
        }
    })));

    // Invalid too big value (amount)
    testInvalidJsonRpc(Fork2, ApiError::InvalidParamsJsonRpc, exp(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : METHOD,
        "params" :
        {
            "asset_id": 1,
            "value" : 1234234200000000000000000000000
        }
    })));

    // Missing value (amount)
    testInvalidJsonRpc(Fork2, ApiError::InvalidParamsJsonRpc, exp(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : METHOD,
        "params" :
        {
            "asset_id": 1
        }
    })));

    // Invalid fee
    testInvalidJsonRpc(Fork2, ApiError::InvalidParamsJsonRpc, exp(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : METHOD,
        "params" :
        {
            "asset_id": 1,
            "value" : 100,
            "fee": 0
        }
    })));

    // Bad coins (string instead of array)
    testInvalidJsonRpc(Fork2, ApiError::InvalidParamsJsonRpc, exp(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : METHOD,
        "params" :
        {
            "asset_id": 1,
            "value" : 12342342,
            "coins": "blah"
        }
    })));

    // Bad coins (int instead of string id)
    testInvalidJsonRpc(Fork2, ApiError::InvalidParamsJsonRpc, exp(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : METHOD,
        "params" :
        {
            "asset_id": 1,
            "value" : 12342342,
            "coins": [22]
        }
    })));

    // Bad session
    testInvalidJsonRpc(Fork2, ApiError::InvalidParamsJsonRpc, exp(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : METHOD,
        "params" :
        {
            "index": 1,
            "value" : 12342342,
            "session": "blah"
        }
    })));

    // Bad txId (not a hex string)
    testInvalidJsonRpc(Fork2, ApiError::InvalidParamsJsonRpc, exp(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : METHOD,
        "params" :
        {
            "asset_id": 1,
            "value" : 12342342,
            "txId": 22
        }
    })));

    // Bad txId string
    testInvalidJsonRpc(Fork2, ApiError::InvalidParamsJsonRpc, exp(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : METHOD,
        "params" :
        {
            "asset_id": 1,
            "value" : 12342342,
            "txId": "22"
        }
    })));

    // obsolette (removed) meta param
    testInvalidJsonRpc(Fork2, ApiError::InvalidParamsJsonRpc, exp(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : METHOD,
        "params" :
        {
            "asset_meta": "some meta",
            "value" : 12342342
        }
    })));

    // blocked before fork2
    testInvalidJsonRpc(NoFork, ApiError::NotSupported, exp(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : METHOD,
        "params" :
        {
            "asset_id": 1,
            "value" : 12342342
        }
    })));

    // blocked before fork2
    testInvalidJsonRpc(Fork1, ApiError::NotSupported, exp(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : METHOD,
        "params" :
        {
            "asset_id": 1,
            "value" : 12342342
        }
    })));

    // blocked if assets disabled, even after fork2
    wallet::g_AssetsEnabled = false;
    testInvalidJsonRpc(Fork3, ApiError::NotSupported, exp(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : METHOD,
        "params" :
        {
            "asset_id": 1,
            "value" : 12342342
        }
    })));
    wallet::g_AssetsEnabled = true;

    // valid asset_id
    testICJsonRpc<T>(exp(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : METHOD,
        "params" :
        {
            "asset_id": 1,
            "value" : 12342342
        }
    })));
}

void TestGetAssetInfo()
{
    // Invalid asset id
    testInvalidJsonRpc(Fork2, ApiError::InvalidParamsJsonRpc, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id"     : 12345,
        "method" : "get_asset_info",
        "params" :
        {
            "asset_id": -1
        }
    }));

    // Invalid meta
    testInvalidJsonRpc(Fork2, ApiError::InvalidParamsJsonRpc, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id"     : 12345,
        "method" : "get_asset_info",
        "params" :
        {
            "asset_meta": ""
        }
    }));

    // missing asset id & meta
    testInvalidJsonRpc(Fork2, ApiError::InvalidParamsJsonRpc, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id"     : 12345,
        "method" : "get_asset_info",
        "params" :
        {
        }
    }));

    // obsolette (rmoved) meta
    testInvalidJsonRpc(Fork2, ApiError::InvalidParamsJsonRpc, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "get_asset_info",
        "params" :
        {
            "asset_meta": "some meta"
        }
    }));

    // disabled until fork2
    testInvalidJsonRpc(NoFork, ApiError::NotSupported, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "get_asset_info",
        "params" :
        {
            "asset_id": 1
        }
    }));

    testInvalidJsonRpc(Fork1, ApiError::NotSupported, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "get_asset_info",
        "params" :
        {
            "asset_id": 1
        }
    }));

    // blocked if assets disabled, even after fork2
    wallet::g_AssetsEnabled = false;
    testInvalidJsonRpc(Fork3, ApiError::NotSupported, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "get_asset_info",
        "params" :
        {
            "asset_id": 1
        }
    }));
    wallet::g_AssetsEnabled = true;

    // valid asset_id
    testGetAssetInfoJsonRpc(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "get_asset_info",
        "params" :
        {
            "asset_id": 1
        }
    }));
}

void TestAITx()
{
    // Invalid asset id
    testInvalidJsonRpc(Fork2, ApiError::InvalidParamsJsonRpc, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id"     : 12345,
        "method" : "tx_asset_info",
        "params" :
        {
            "asset_id": -1
        }
    }));

    // Invalid meta
    testInvalidJsonRpc(Fork2, ApiError::InvalidParamsJsonRpc, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id"     : 12345,
        "method" : "tx_asset_info",
        "params" :
        {
            "asset_meta": ""
        }
    }));

    // missing asset id & meta
    testInvalidJsonRpc(Fork2, ApiError::InvalidParamsJsonRpc, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id"     : 12345,
        "method" : "tx_asset_info",
        "params" :
        {
        }
    }));

    // Bad txId (not a hex string)
    testInvalidJsonRpc(Fork2, ApiError::InvalidParamsJsonRpc, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "tx_asset_info",
        "params" :
        {
            "asset_id": 1,
            "txId": 22
        }
    }));

    // Bad txId string
    testInvalidJsonRpc(Fork2, ApiError::InvalidParamsJsonRpc, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "tx_asset_info",
        "params" :
        {
            "asset_id": 1,
            "txId": "22"
        }
    }));

    // obsolette (removed) meta
    testInvalidJsonRpc(Fork2, ApiError::InvalidParamsJsonRpc, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "tx_asset_info",
        "params" :
        {
            "asset_meta": "some meta"
        }
    }));

    // disabled before fork2
    testInvalidJsonRpc(NoFork, ApiError::NotSupported, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "tx_asset_info",
        "params" :
        {
            "asset_id": 1
        }
    }));

    testInvalidJsonRpc(Fork1, ApiError::NotSupported, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "tx_asset_info",
        "params" :
        {
            "asset_id": 1
        }
    }));

    // blocked if assets disabled, even after fork2
    wallet::g_AssetsEnabled = false;
    testInvalidJsonRpc(Fork3, ApiError::NotSupported, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "tx_asset_info",
        "params" :
        {
            "asset_id": 1
        }
    }));
    wallet::g_AssetsEnabled = true;

    // valid asset_id
    testAIJsonRpc(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "tx_asset_info",
        "params" :
        {
            "asset_id": 1
        }
    }));
}

void TestAssetsAPI()
{
    //
    // EXPLICITLY ENABLE Confidential assets to perform tests
    //
    TestICTx<Issue>("tx_asset_issue");
    TestICTx<Consume>("tx_asset_consume");
    TestAITx();
    TestGetAssetInfo();
}

void testCalcChange()
{
    std::string msg = JSON_CODE(
        {
            "jsonrpc":"2.0",
            "id" : 4,
            "method" : "calc_change",
            "params" :
            {
                "amount" : 1234,
                "asset_id" : 2,
                "fee" : 10000,
                "is_push_transaction" : true
            }
        });
    struct ApiTest : public WalletApiTest
    {
        void onAPIError(const json& msg) override
        {
            m_Failed = true;
        }

        void onHandleCalcChange(const JsonRpcId& id, CalcChange&& data) override
        {
            WALLET_CHECK(id == 4);

            WALLET_CHECK(data.amount == 1234);
            WALLET_CHECK(data.assetId && *data.assetId == 2);
            WALLET_CHECK(data.explicitFee == 10000);
            WALLET_CHECK(data.isPushTransaction == true);
        }

        explicit ApiTest(Fork fork) : WalletApiTest(fork, ApiInitData()) {}
        bool m_Failed = false;
    };

    {
        // no assets support
        ApiTest apiNoFork(NoFork);
        WALLET_CHECK(ApiSyncMode::DoneSync == apiNoFork.executeAPIRequest(msg.data(), msg.size()));
        WALLET_CHECK(apiNoFork.m_Failed == true);
    }

    ApiTest api(Fork2);
    WALLET_CHECK(ApiSyncMode::DoneSync == api.executeAPIRequest(msg.data(), msg.size()));
    WALLET_CHECK(api.m_Failed == false);

    {
        json res;
        CalcChange::Response response = {};
        response.assetChange = 100;
        response.change = 8000000000;
        response.explicitFee = 700000000;

        api.getResponse(123, response, res);
        testResultHeader(res);

        WALLET_CHECK(res["id"] == 123);
        auto& r = res["result"];
        WALLET_CHECK(r["asset_change"] == 100);
        WALLET_CHECK(r["asset_change_str"] == "100");
        WALLET_CHECK(r["change"] == 8000000000);
        WALLET_CHECK(r["change_str"] == "8000000000");
        WALLET_CHECK(r["explicit_fee"] == 700000000);
        WALLET_CHECK(r["explicit_fee_str"] == "700000000");
        WALLET_CHECK(r.size() == 6);
    }
    testInvalidJsonRpc(Fork3, ApiError::InvalidParamsJsonRpc, JSON_CODE(
    {
        "jsonrpc":"2.0",
        "id" : 4,
        "method" : "calc_change",
        "params" :
        {

        }
    }));
}

namespace
{
    //
    // Apps access tests that run the real V6Api handlers on a real wallet DB.
    // Objects are tagged the same way the API tags them: txs get TxParameterID::AppID,
    // addresses get the app id as category.
    //
    const char kAppsDBName[] = "wallet_api_apps_test.db";
    const char kAppA[]       = "appid-a";
    const char kAppAName[]   = "App A";
    const char kAppB[]       = "appid-b";
    const char kAppBName[]   = "App B";

    class DbApiClient
        : public wallet::V6Api
        , public IWalletApiHandler
    {
    public:
        DbApiClient(IWalletDB::Ptr walletDB, Wallet::Ptr wallet, const std::string& appId = std::string(), const std::string& appName = std::string())
            : V6Api(*this, makeInitData(walletDB, wallet, appId, appName))
        {
        }

        void sendAPIResponse(const json& resp) override
        {
            _last = resp;
        }

        void onParseError(const json& msg) override
        {
            _last = msg;
        }

        Height get_TipHeight() const override
        {
            return Rules::get().pForks[3].m_Height;
        }

        json call(const std::string& method, const json& params = json::object())
        {
            const json req = {
                {"jsonrpc", "2.0"},
                {"id",      ++_callId},
                {"method",  method},
                {"params",  params}
            };

            const auto sreq = req.dump();
            _last = json();
            WALLET_CHECK(ApiSyncMode::DoneSync == executeAPIRequest(sreq.data(), sreq.size()));
            return _last;
        }

    private:
        static ApiInitData makeInitData(IWalletDB::Ptr walletDB, Wallet::Ptr wallet, const std::string& appId, const std::string& appName)
        {
            ApiInitData data;
            data.walletDB = std::move(walletDB);
            data.wallet   = std::move(wallet);
            data.appId    = appId;
            data.appName  = appName;
            return data;
        }

        int  _callId = 0;
        json _last;
    };

    bool isResult(const json& resp)
    {
        return resp.find("result") != resp.end() && resp.find("error") == resp.end();
    }

    bool isError(const json& resp, ApiError code)
    {
        if (resp.find("error") == resp.end())
        {
            return false;
        }
        const ApiError actual = resp["error"]["code"];
        return actual == code;
    }

    struct AppsTestDB
    {
        IWalletDB::Ptr walletDB;
        Wallet::Ptr    wallet;

        AppsTestDB()
        {
            boost::filesystem::remove(kAppsDBName);

            ECC::NoLeak<ECC::uintBig> seed;
            seed.V = 10283UL;
            walletDB = WalletDB::init(kAppsDBName, std::string("pass123"), seed);
            wallet   = std::make_shared<Wallet>(walletDB);
        }

        ~AppsTestDB()
        {
            wallet.reset();
            walletDB.reset();
            boost::filesystem::remove(kAppsDBName);
        }

        TxID storeTx(const std::string& appId, const std::string& appName, TxStatus status)
        {
            TxDescription tx(GenerateTxID(), TxType::Simple, 1000, 100);
            tx.m_status     = status;
            tx.m_sender     = true;
            tx.m_createTime = getTimestamp();
            walletDB->saveTx(tx);

            if (!appId.empty())
            {
                // this is what tx_send / invoke_contract store for app txs
                storage::setTxParameter(*walletDB, tx.m_txId, TxParameterID::AppID, appId, true);
                storage::setTxParameter(*walletDB, tx.m_txId, TxParameterID::AppName, appName, true);
            }
            return tx.m_txId;
        }

        std::string storeAddress(const std::string& category)
        {
            WalletAddress addr;
            walletDB->createAddress(addr);
            addr.setCategory(category);
            walletDB->saveAddress(addr);
            return addr.m_Token;
        }
    };

    std::set<std::string> listTxIds(DbApiClient& client)
    {
        std::set<std::string> res;
        const auto resp = client.call("tx_list");
        WALLET_CHECK(isResult(resp));
        if (isResult(resp))
        {
            for (const auto& tx : resp["result"])
            {
                res.insert(tx["txId"].get<std::string>());
            }
        }
        return res;
    }

    std::set<std::string> listAddrTokens(DbApiClient& client)
    {
        std::set<std::string> res;
        const auto resp = client.call("addr_list", json{{"own", true}});
        WALLET_CHECK(isResult(resp));
        if (isResult(resp))
        {
            for (const auto& addr : resp["result"])
            {
                res.insert(addr["address"].get<std::string>());
            }
        }
        return res;
    }

    void TestAppsTxAccess()
    {
        cout << "\nTesting apps access to transactions (real wallet DB)...\n";

        AppsTestDB db;
        const auto txA = std::to_string(db.storeTx(kAppA, kAppAName, TxStatus::Completed));
        const auto txB = std::to_string(db.storeTx(kAppB, kAppBName, TxStatus::Completed));
        const auto txN = std::to_string(db.storeTx("", "", TxStatus::Completed));

        DbApiClient wallet(db.walletDB, db.wallet);
        DbApiClient appA(db.walletDB, db.wallet, kAppA, kAppAName);
        DbApiClient appB(db.walletDB, db.wallet, kAppB, kAppBName);
        DbApiClient appC(db.walletDB, db.wallet, "appid-c", "App C");

        // tx_list: a normal client sees everything, an app sees only its own txs
        WALLET_CHECK((listTxIds(wallet) == std::set<std::string>{txA, txB, txN}));
        WALLET_CHECK((listTxIds(appA)   == std::set<std::string>{txA}));
        WALLET_CHECK((listTxIds(appB)   == std::set<std::string>{txB}));
        WALLET_CHECK(listTxIds(appC).empty());

        {
            const auto resp = appA.call("tx_list");
            WALLET_CHECK(isResult(resp) && resp["result"].size() == 1);
            if (isResult(resp) && resp["result"].size() == 1)
            {
                WALLET_CHECK(resp["result"][0]["appid"] == kAppA);
                WALLET_CHECK(resp["result"][0]["appname"] == kAppAName);
            }
        }

        // tx_status: foreign and non-app txs look like unknown ids to an app
        for (const auto& id : {txA, txB, txN})
        {
            WALLET_CHECK(isResult(wallet.call("tx_status", json{{"txId", id}})));
        }
        WALLET_CHECK(isResult(appA.call("tx_status", json{{"txId", txA}})));
        WALLET_CHECK(isError(appA.call("tx_status", json{{"txId", txB}}), ApiError::InvalidParamsJsonRpc));
        WALLET_CHECK(isError(appA.call("tx_status", json{{"txId", txN}}), ApiError::InvalidParamsJsonRpc));
        WALLET_CHECK(isError(appB.call("tx_status", json{{"txId", txA}}), ApiError::InvalidParamsJsonRpc));

        // tx_cancel: access check comes first. Own tx -> "invalid status" (not active), foreign -> "unknown id"
        WALLET_CHECK(isError(appA.call("tx_cancel", json{{"txId", txA}}), ApiError::InvalidTxStatus));
        WALLET_CHECK(isError(appA.call("tx_cancel", json{{"txId", txB}}), ApiError::InvalidParamsJsonRpc));
        WALLET_CHECK(isError(appA.call("tx_cancel", json{{"txId", txN}}), ApiError::InvalidParamsJsonRpc));

        // tx_delete: an app cannot delete foreign txs, they stay in the DB
        WALLET_CHECK(isError(appA.call("tx_delete", json{{"txId", txB}}), ApiError::InvalidParamsJsonRpc));
        WALLET_CHECK(isError(appA.call("tx_delete", json{{"txId", txN}}), ApiError::InvalidParamsJsonRpc));
        WALLET_CHECK((listTxIds(wallet) == std::set<std::string>{txA, txB, txN}));

        WALLET_CHECK(isResult(appA.call("tx_delete", json{{"txId", txA}})));
        WALLET_CHECK(isResult(wallet.call("tx_delete", json{{"txId", txN}})));
        WALLET_CHECK((listTxIds(wallet) == std::set<std::string>{txB}));
        WALLET_CHECK(listTxIds(appA).empty());
        WALLET_CHECK((listTxIds(appB) == std::set<std::string>{txB}));
    }

    void TestAppsPaymentProofAccess()
    {
        cout << "\nTesting apps access to payment proofs (real wallet DB)...\n";

        AppsTestDB db;
        const auto txA = std::to_string(db.storeTx(kAppA, kAppAName, TxStatus::InProgress));
        const auto txB = std::to_string(db.storeTx(kAppB, kAppBName, TxStatus::InProgress));
        const auto txN = std::to_string(db.storeTx("", "", TxStatus::InProgress));

        DbApiClient wallet(db.walletDB, db.wallet);
        DbApiClient appA(db.walletDB, db.wallet, kAppA, kAppAName);

        auto proofError = [](const json& resp) -> std::string
        {
            if (!isError(resp, ApiError::PaymentProofExportError) || resp["error"].find("data") == resp["error"].end())
            {
                return "";
            }
            return resp["error"]["data"].get<std::string>();
        };

        // Access allowed: the call gets as far as the tx status check
        WALLET_CHECK(proofError(appA.call("export_payment_proof", json{{"txId", txA}})) == kErrorPpExportFailedTxNotCompleted);
        WALLET_CHECK(proofError(wallet.call("export_payment_proof", json{{"txId", txB}})) == kErrorPpExportFailedTxNotCompleted);
        WALLET_CHECK(proofError(wallet.call("export_payment_proof", json{{"txId", txN}})) == kErrorPpExportFailedTxNotCompleted);

        // Access denied: reported exactly like a missing tx
        WALLET_CHECK(proofError(appA.call("export_payment_proof", json{{"txId", txB}})) == kErrorPpExportFailed);
        WALLET_CHECK(proofError(appA.call("export_payment_proof", json{{"txId", txN}})) == kErrorPpExportFailed);
    }

    void TestAppsAddressAccess()
    {
        cout << "\nTesting apps access to addresses (real wallet DB)...\n";

        AppsTestDB db;
        const auto addrA = db.storeAddress(kAppA);
        const auto addrB = db.storeAddress(kAppB);
        const auto addrN = db.storeAddress("");

        DbApiClient wallet(db.walletDB, db.wallet);
        DbApiClient appA(db.walletDB, db.wallet, kAppA, kAppAName);
        DbApiClient appB(db.walletDB, db.wallet, kAppB, kAppBName);

        // addr_list: a normal client sees all own addresses, an app sees only the ones it created
        {
            const auto all = listAddrTokens(wallet);
            WALLET_CHECK(all.count(addrA) == 1 && all.count(addrB) == 1 && all.count(addrN) == 1);
        }
        WALLET_CHECK((listAddrTokens(appA) == std::set<std::string>{addrA}));
        WALLET_CHECK((listAddrTokens(appB) == std::set<std::string>{addrB}));

        // create_address by an app: tagged with the app id and visible to that app only
        {
            const auto resp = appA.call("create_address", json{{"comment", "app address"}});
            WALLET_CHECK(isResult(resp));
            if (isResult(resp))
            {
                const auto token = resp["result"].get<std::string>();
                const auto stored = db.walletDB->getAddressByToken(token);
                WALLET_CHECK(stored && stored->m_category == kAppA);
                WALLET_CHECK((listAddrTokens(appA) == std::set<std::string>{addrA, token}));
                WALLET_CHECK(listAddrTokens(appB).count(token) == 0);
                WALLET_CHECK(listAddrTokens(wallet).count(token) == 1);
            }
        }

        // edit_address / delete_address: foreign addresses look like missing ones
        WALLET_CHECK(isError(appA.call("edit_address", json{{"address", addrB}, {"comment", "x"}}), ApiError::InvalidAddress));
        WALLET_CHECK(isError(appA.call("edit_address", json{{"address", addrN}, {"comment", "x"}}), ApiError::InvalidAddress));
        WALLET_CHECK(isError(appA.call("delete_address", json{{"address", addrB}}), ApiError::InvalidAddress));
        WALLET_CHECK(isError(appA.call("delete_address", json{{"address", addrN}}), ApiError::InvalidAddress));
        WALLET_CHECK(db.walletDB->getAddressByToken(addrB));
        WALLET_CHECK(db.walletDB->getAddressByToken(addrN));

        WALLET_CHECK(isResult(appA.call("edit_address", json{{"address", addrA}, {"comment", "renamed"}})));
        {
            const auto stored = db.walletDB->getAddressByToken(addrA);
            WALLET_CHECK(stored && stored->m_label == "renamed");
        }
        WALLET_CHECK(isResult(appA.call("delete_address", json{{"address", addrA}})));
        WALLET_CHECK(!db.walletDB->getAddressByToken(addrA));
        WALLET_CHECK(db.walletDB->getAddressByToken(addrB));
    }

    void testAppsDbAccess()
    {
        io::Reactor::Ptr reactor{ io::Reactor::create() };
        io::Reactor::Scope scope(*reactor);

        TestAppsTxAccess();
        TestAppsPaymentProofAccess();
        TestAppsAddressAccess();
    }
}

thread_local const beam::Rules* beam::Rules::s_pInstance = nullptr;

int main()
{
    wallet::g_AssetsEnabled = true;

    beam::Rules r;
    beam::Rules::Scope scopeRules(r);

    r.pForks[1].m_Height = 30;
    r.pForks[2].m_Height = 60;
    r.pForks[3].m_Height = 90;
    r.UpdateChecksum();

    auto logger = beam::Logger::create();
    testInvalidJsonRpc(NoFork, [](const json& msg)
    {
        testErrorHeader(msg);
        CHECK_JSON_FIELD_ABSENT(msg, "id");
        WALLET_CHECK(msg["error"]["code"] == ApiError::InvalidJsonRpc);
    }, JSON_CODE({}));

    testInvalidJsonRpc(NoFork, [](const json& msg)
    {
        testErrorHeader(msg);

        CHECK_JSON_FIELD_ABSENT(msg, "id");
        WALLET_CHECK(msg["error"]["code"] == ApiError::InvalidJsonRpc);
    }, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "method" : 1,
        "params" : "bar"
    }));

    testInvalidJsonRpc(NoFork, [](const json& msg)
    {
        testErrorHeaderWithId(msg);

        WALLET_CHECK(msg["id"] == 123);
        WALLET_CHECK(msg["error"]["code"] == ApiError::NotFoundJsonRpc);
    }, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 123,
        "method" : "balance123",
        "key" : "0123456789AbcDef8b7cb3804b5978d42312c841dbfa03a1c31fc2f0627eeed6e43f2",
        "params" : "bar"
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

    testDefaultCreateAddressJsonRpc(JSON_CODE(
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


    // asset_id not allowed before fork2
    testInvalidJsonRpc(NoFork, ApiError::NotSupported, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "get_utxo",
        "params":
        {
            "filter":
            {
                "asset_id": 1
            }
        }
    }));

    // asset_id not allowed before fork2
    testInvalidJsonRpc(Fork1, ApiError::NotSupported, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "get_utxo",
        "params":
        {
            "filter":
            {
                "asset_id": 1
            }
        }
    }));

    // asset_id not allowed if assets disabled, even after fork2
    wallet::g_AssetsEnabled = false;
    testInvalidJsonRpc(Fork2, ApiError::NotSupported, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "get_utxo",
        "params":
        {
            "filter":
            {
                "asset_id": 1
            }
        }
    }));
    wallet::g_AssetsEnabled = true;

    // assets enabled, fork2, correct asset_id
    testGetUtxoJsonRpc(Fork2, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "get_utxo",
        "params":
        {
            "filter":
            {
                "asset_id": 1
            }
        }
    }));

    // asset_id not allowed before fork2
    testInvalidJsonRpc(NoFork, ApiError::NotSupported, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "tx_send",
        "params" :
        {
            "asset_id": 1,
            "value" : 12342342,
            "address" : "472e17b0419055ffee3b3813b98ae671579b0ac0dcd6f1a23b11a75ab148cc67"
        }
    }));

    // asset_id not allowed before fork2
    testInvalidJsonRpc(Fork1, ApiError::NotSupported, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "tx_send",
        "params" :
        {
            "asset_id": 1,
            "value" : 12342342,
            "address" : "472e17b0419055ffee3b3813b98ae671579b0ac0dcd6f1a23b11a75ab148cc67"
        }
    }));

    // asset_id not allowed if assets disabled, even after fork2
    wallet::g_AssetsEnabled = false;
    testInvalidJsonRpc(Fork2, ApiError::NotSupported, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "tx_send",
        "params" :
        {
            "asset_id": 1,
            "value" : 12342342,
            "address" : "472e17b0419055ffee3b3813b98ae671579b0ac0dcd6f1a23b11a75ab148cc67"
        }
    }));
    wallet::g_AssetsEnabled = true;

    testSendJsonRpc(Fork2, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "tx_send",
        "params" : 
        {
            "asset_id": 1,
            "value" : 12342342,
            "address" : "472e17b0419055ffee3b3813b98ae671579b0ac0dcd6f1a23b11a75ab148cc67"
        }
    }));

    testInvalidJsonRpc(Fork2, ApiError::InvalidAddress, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "tx_send",
        "params" :
        {
            "value" : 12342342,
            "from" : "wagagel",
            "asset_id": 1,
            "address" : "472e17b0419055ffee3b3813b98ae671579b0ac0dcd6f1a23b11a75ab148cc67"
        }
    }));

    testSendJsonRpc(Fork2, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "tx_send",
        "params" : 
        {
            "asset_id": 1,
            "value" : 12342342,
            "from" : "19d0adff5f02787819d8df43b442a49b43e72a8b0d04a7cf995237a0422d2be83b6",
            "address" : "472e17b0419055ffee3b3813b98ae671579b0ac0dcd6f1a23b11a75ab148cc67"
        }
    }));

    // value is too big
    testInvalidJsonRpc(Fork2, ApiError::InvalidParamsJsonRpc, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "tx_send",
        "params" :
        {
            "asset_id": 1,
            "value" : 1234234200000000000000000000000,
            "from" : "19d0adff5f02787819d8df43b442a49b43e72a8b0d04a7cf995237a0422d2be83b6",
            "address" : "472e17b0419055ffee3b3813b98ae671579b0ac0dcd6f1a23b11a75ab148cc67"
        }
    }));

    testInvalidJsonRpc(Fork2, ApiError::InvalidAddress, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "tx_send",
        "params" :
        {
            "value" : 12342342,
            "from" : "19d0adff5f02787819d8df43b442a49b43e72a8b0d04a7cf995237a0422d2be83b6",
            "address" : "wagagel",
            "asset_id": 1
        }
    }));

    // bad asset_id
    testInvalidJsonRpc(Fork2, ApiError::InvalidParamsJsonRpc, JSON_CODE({
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "tx_send",
        "params" :
        {
            "value" : 20,
            "asset_id": -1,
            "address" : "19d0adff5f02787819d8df43b442a49b43e72a8b0d04a7cf995237a0422d2be83b6"
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

    testSplitJsonRpc(Fork2, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "tx_split",
        "params" :
        {
            "coins" : [11, 12, 13, 50000000000000],
            "fee" : 100,
            "asset_id": 1
        }
    }));

    testInvalidJsonRpc(NoFork, ApiError::InvalidParamsJsonRpc, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "tx_split",
        "params" :
        {
            "coins" : [11, -12, 13, 50000000000000] ,
            "fee" : 4
        }
    }));

    // asset_id not allowed before fork2
    testInvalidJsonRpc(NoFork, ApiError::NotSupported, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "tx_split",
        "params" :
        {
            "coins" : [11, -12, 13, 50000000000000] ,
            "fee" : 4,
            "asset_id": 1
        }
    }));

    // asset_id not allowed before fork2
    testInvalidJsonRpc(Fork1, ApiError::NotSupported, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "tx_split",
        "params" :
        {
            "coins" : [11, -12, 13, 50000000000000] ,
            "fee" : 4,
            "asset_id": 1
        }
    }));

    // asset_id not allowed if assets disabled, even after fork2
    wallet::g_AssetsEnabled = false;
    testInvalidJsonRpc(Fork2, ApiError::NotSupported, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "tx_split",
        "params" :
        {
            "coins" : [11, -12, 13, 50000000000000] ,
            "fee" : 4,
            "asset_id": 1
        }
    }));
    wallet::g_AssetsEnabled = true;

    testTxListJsonRpc(Fork2, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "tx_list",
        "params" :
        {
            "filter" : 
            {
                "status" : 3,
                "asset_id": 1
            }
        }
    }));

    // asset_id not allowed before fork2
    testInvalidJsonRpc(NoFork, ApiError::NotSupported, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "tx_list",
        "params" :
        {
            "filter" :
            {
                "status" : 3,
                "asset_id": 1
            }
        }
    }));

    // asset_id not allowed before fork2
    testInvalidJsonRpc(Fork1, ApiError::NotSupported, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "tx_list",
        "params" :
        {
            "filter" :
            {
                "status" : 3,
                "asset_id": 1
            }
        }
    }));

    // asset_id not allowed if assets disabled, even after fork2
    wallet::g_AssetsEnabled = false;
    testInvalidJsonRpc(Fork2, ApiError::NotSupported, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "tx_list",
        "params" :
        {
            "filter" :
            {
                "status" : 3,
                "asset_id": 1
            }
        }
    }));
    wallet::g_AssetsEnabled = true;

    testTxListPaginationJsonRpc(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "tx_list",
        "params" :
        {
            "skip" : 10,
            "count" : 10
        }
    }));

    testValidateAddressJsonRpc(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "validate_address",
        "params" :
        {
            "address" : "wagagel"
        }
    }), false);

    testValidateAddressJsonRpc(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "validate_address",
        "params" :
        {
            "address" : "472e17b0419055ffee3b3813b98ae671579b0ac0dcd6f1a23b11a75ab148cc67"
        }
    }), true);

    testJsonRpcIdAsValue(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : "123",
        "method" : "create_address"
    }), "123");

    testJsonRpcIdAsValue(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 123,
        "method" : "create_address"
    }), 123);

    testJsonRpcIdAsValue(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 0,
        "method" : "create_address"
    }), 0);

    testJsonRpcIdAsValue(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : -123,
        "method" : "create_address"
    }), -123);

    testJsonRpcIdAsValue(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 2147483647,
        "method" : "create_address"
    }), 2147483647);

    testJsonRpcIdAsValue(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 2147483648,
        "method" : "create_address"
    }), 2147483648);

    testInvalidJsonRpc(NoFork, [](const json& msg)
    {
        testErrorHeader(msg);
        CHECK_JSON_FIELD_ABSENT(msg, "id");
        WALLET_CHECK(msg["error"]["code"] == ApiError::InvalidJsonRpc);
    }, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 1.23
    }));

    testInvalidJsonRpc(NoFork, [](const json& msg)
    {
        testErrorHeader(msg);

        CHECK_JSON_FIELD_ABSENT(msg, "id");
        WALLET_CHECK(msg["error"]["code"] == ApiError::InvalidJsonRpc);
    }, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : null
    }));

    testGenerateTxIdJsonRpc(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : "123",
        "method" : "generate_tx_id"
    }));

    testExportPaymentProofJsonRpc(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : "123",
        "method" : "export_payment_proof",
        "params" :
        {
            "txId" : "10c4b760c842433cb58339a0fafef3db"
        }
    }));

    testVerifyPaymentProofJsonRpc(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : "123",
        "method" : "verify_payment_proof",
        "params" :
        {
            "payment_proof" : "8009f28991ef543253c8b6a2caf15cf99e23fb9c2b4ca30dc463c8ceb354d7979e80ef7d4255dd5e885200648abe5826d8e0ba0157d3e8cf9c42dcc8258b036986e50400371789ee82afc25ee29c9c57bcb1018b725a3a94c0ceb1fa7984ea13de4982553e0d78d925a362982182a971e654857b8e407e7ad2e9cb72b2b8228812f8ec50435351000c94e2c85996e9527d9b0c90a1843205a7ec8f99fa534083e5f1d055d9f53894"
        }
    }));


#ifdef BEAM_ATOMIC_SWAP_SUPPORT
    testGetBalanceJsonRpc(JSON_CODE(
        {
            "jsonrpc": "2.0",
            "id" : "123",
            "method" : "swap_get_balance",
            "params" :
            {
                "coin": "ltc"
            }
        }));

    testDecodeTokenJsonRpc(JSON_CODE(
        {
            "jsonrpc": "2.0",
            "id" : "123",
            "method" : "swap_decode_token",
            "params" :
            {
                "token": "6xfNAUemTbmp7KRCRydiGStMZe6oRh59LzS7uk1V4eTrUX1mKcCGY7jdtMtSs4XLt6Ug8jWnepMEZCrqSUw7PeKRDZ8yyVZu1WHXzootpybBjX3nVxxHRSdk4ncBGDh1cssmiJhswZC9PfsaJmRKqXJM3x9tcX7EZn5Vjg8"
            }
        }));

    testOfferStatusJsonRpc(JSON_CODE(
        {
            "jsonrpc": "2.0",
            "id" : "123",
            "method" : "swap_offer_status",
            "params" :
            {
                "tx_id": "b35fd69030694009b8bf849140d9319e"
            }
        }));
#endif  // BEAM_ATOMIC_SWAP_SUPPORT

    TestAssetsAPI();

    // empty args
    testInvalidJsonRpc(Fork3, ApiError::InvalidParamsJsonRpc, JSON_CODE(
         {
            "jsonrpc": "2.0",
            "id": "123",
            "method": "invoke_contract",
            "params":
            {
                "args": ""
            }
        }));

    // non-string args
    testInvalidJsonRpc(Fork3, ApiError::InvalidParamsJsonRpc, JSON_CODE(
         {
            "jsonrpc": "2.0",
            "id": "123",
            "method": "invoke_contract",
            "params":
            {
                "args": 22
            }
        }));

    // non-array contract
    testInvalidJsonRpc(Fork3, ApiError::InvalidParamsJsonRpc, JSON_CODE(
         {
            "jsonrpc": "2.0",
            "id": "123",
            "method": "invoke_contract",
            "params":
            {
                "contract": 22
            }
        }));

    // empty array contract
    testInvalidJsonRpc(Fork3, ApiError::InvalidParamsJsonRpc, JSON_CODE(
         {
            "jsonrpc": "2.0",
            "id": "123",
            "method": "invoke_contract",
            "params":
            {
                "contract": []
            }
        }));

    // non-byte array contract
    testInvalidJsonRpc(Fork3, ApiError::InvalidParamsJsonRpc, JSON_CODE(
         {
            "jsonrpc": "2.0",
            "id": "123",
            "method": "invoke_contract",
            "params":
            {
                "contract": ["a", "b", "c"]
            }
        }));

    // non-sting contract_file
    testInvalidJsonRpc(Fork3, ApiError::InvalidParamsJsonRpc, JSON_CODE(
         {
            "jsonrpc": "2.0",
            "id": "123",
            "method": "invoke_contract",
            "params":
            {
                "contract_file": 22
            }
        }));

    // empty contract_file
    testInvalidJsonRpc(Fork3, ApiError::InvalidParamsJsonRpc, JSON_CODE(
         {
            "jsonrpc": "2.0",
            "id": "123",
            "method": "invoke_contract",
            "params":
            {
                "contract_file": ""
            }
        }));

    // non-bool create_tx
    testInvalidJsonRpc(Fork3, ApiError::InvalidParamsJsonRpc, JSON_CODE(
         {
            "jsonrpc": "2.0",
            "id": "123",
            "method": "invoke_contract",
            "params":
            {
                "create_tx": "NO"
            }
        }));

    // missing data
    testInvalidJsonRpc(Fork3, ApiError::InvalidParamsJsonRpc, JSON_CODE(
         {
            "jsonrpc": "2.0",
            "id": "123",
            "method": "process_invoke_data",
            "params":
            {
            }
        }));

    // non-array data
    testInvalidJsonRpc(Fork3, ApiError::InvalidParamsJsonRpc, JSON_CODE(
         {
            "jsonrpc": "2.0",
            "id": "123",
            "method": "process_invoke_data",
            "params":
            {
                "data": "data"
            }
        }));

    // empty array data
    testInvalidJsonRpc(Fork3, ApiError::InvalidParamsJsonRpc, JSON_CODE(
         {
            "jsonrpc": "2.0",
            "id": "123",
            "method": "process_invoke_data",
            "params":
            {
                "data": []
            }
        }));

    // non-string array data
    testInvalidJsonRpc(Fork3, ApiError::InvalidParamsJsonRpc, JSON_CODE(
         {
            "jsonrpc": "2.0",
            "id": "123",
            "method": "process_invoke_data",
            "params":
            {
                "data": ["123", "456", "789"]
            }
        }));

    testAppsApi();
    testCalcChange();
    testAppsDbAccess();

    return WALLET_CHECK_RESULT;
}
