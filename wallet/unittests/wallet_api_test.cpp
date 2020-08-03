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

#include "wallet/api/api.h"
#include "utility/logger.h"
#include "nlohmann/json.hpp"

using namespace std;
using namespace beam;
using namespace beam::wallet;
using json = nlohmann::json;

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

    class WalletApiHandlerBase : public wallet::IWalletApiHandler
    {
        void onInvalidJsonRpc(const json& msg) override {}
        
#define MESSAGE_FUNC(strct, name, _) virtual void onMessage(const JsonRpcId& id, const strct& data) override {};
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

                cout << msg["error"] << endl;
            }

            void onMessage(const JsonRpcId& id, const CreateAddress& data) override 
            {
                WALLET_CHECK(id > 0);
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

    void testGetUtxoJsonRpc(const std::string& msg)
    {
        class WalletApiHandler : public WalletApiHandlerBase
        {
        public:

            void onInvalidJsonRpc(const json& msg) override
            {
                WALLET_CHECK(!"invalid get_utxo api json!!!");

                cout << msg["error"] << endl;
            }

            void onMessage(const JsonRpcId& id, const GetUtxo& data) override
            {
                WALLET_CHECK(id > 0);
                WALLET_CHECK(data.filter.assetId && *data.filter.assetId == 1);
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
                coin.m_maturity = 60;
				coin.m_confirmHeight = 60;
				coin.m_status = Coin::Status::Available; // maturity is returned only for confirmed coins
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
                WALLET_CHECK(Coin::FromString(result[i]["id"])->m_Idx == uint64_t(132 + i));
                WALLET_CHECK(result[i]["amount"] == 1234 + i);
                WALLET_CHECK(result[i]["type"] == "norm");
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

                cout << msg["error"] << endl;
            }

            void onMessage(const JsonRpcId& id, const Send& data) override
            {
                WALLET_CHECK(id > 0);

                WALLET_CHECK(data.session && *data.session == 15);
                WALLET_CHECK(data.value == 12342342);
                WALLET_CHECK(to_string(data.address) == "472e17b0419055ffee3b3813b98ae671579b0ac0dcd6f1a23b11a75ab148cc67");
                WALLET_CHECK(data.assetId && *data.assetId == 1);

                if(data.from)
                {
                    WALLET_CHECK(to_string(*data.from) == "19d0adff5f02787819d8df43b442a49b43e72a8b0d04a7cf995237a0422d2be83b6");
                }
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

    using TestErrorFunc = std::function<void(const json& msg)>;
    template <typename T> using TestSuccessFunc = std::function<void(const JsonRpcId& id, const T& data)>;
    using TestFinishFunc = std::function<void()>;

    template<typename T> void testJsonRpc(const std::string& msg
        , TestErrorFunc onError
        , TestSuccessFunc<T> onSuccess = []() {}
        , TestFinishFunc onFinish = []() {})
    {
        class WalletApiHandler : public WalletApiHandlerBase
        {
        public:

            WalletApiHandler(TestErrorFunc onError, std::function<void(const JsonRpcId& id, const T& data)> onSuccess) 
                : _onError(onError), _onSuccess(onSuccess) {}

            void onInvalidJsonRpc(const json& msg) override { _onError(msg); }
            void onMessage(const JsonRpcId& id, const T& data) override { _onSuccess(id, data); }

            TestErrorFunc _onError;
            std::function<void(const JsonRpcId& id, const T& data)> _onSuccess;
        };

        WalletApiHandler handler(onError, onSuccess);
        WalletApi api(handler);

        WALLET_CHECK(api.parse(msg.data(), msg.size()));

        {
            json res;
            typename T::Response response;

            api.getResponse(123, response, res);
            testResultHeader(res);

            WALLET_CHECK(res["id"] == 123);
            onFinish();
        }
    }

    void testInvalidSendJsonRpc(const std::string& msg)
    {
        class WalletApiHandler : public WalletApiHandlerBase
        {
        public:

            void onInvalidJsonRpc(const json& msg) override
            {
                cout << msg["error"] << endl;
            }

            void onMessage(const JsonRpcId& id, const Send& data) override 
            {
                WALLET_CHECK(!"error, only onInvalidJsonRpc() should be called!!!");
            }
        };

        WalletApiHandler handler;
        WalletApi api(handler);

        WALLET_CHECK(api.parse(msg.data(), msg.size()));
    }

    template<typename T>
    void testInvalidAssetJsonRpc(const std::string& msg)
    {
        class WalletApiHandler : public WalletApiHandlerBase
        {
        public:
            void onInvalidJsonRpc(const json& msg) override
            {
                cout << msg["error"] << endl;
            }

            void onMessage(const JsonRpcId& id, const T& data) override
            {
                WALLET_CHECK(!"error, only onInvalidJsonRpc() should be called!!!");
            }
        };

        WalletApiHandler handler;
        WalletApi api(handler);
        WALLET_CHECK(api.parse(msg.data(), msg.size()));
    }

    template<typename T>
    void testICJsonRpc(const std::string& msg)
    {
        class WalletApiHandler : public WalletApiHandlerBase
        {
        public:
            void onInvalidJsonRpc(const json& msg) override
            {
                WALLET_CHECK(!"invalid issue/consume api json!!!");
                cout << msg["error"] << endl;
            }

            void onMessage(const JsonRpcId& id, const T& data) override
            {
                WALLET_CHECK(id > 0);
                WALLET_CHECK((data.assetId && *data.assetId > 0) || (data.assetMeta && !data.assetMeta->empty()));
                WALLET_CHECK(data.value > 0);
            }
        };

        WalletApiHandler handler;
        WalletApi api(handler);
        WALLET_CHECK(api.parse(msg.data(), msg.size()));

        {
            json res;
            typename T::Response status;
            status.txId = { 1,2,3 };
            api.getResponse(12345, status, res);
            testResultHeader(res);

            WALLET_CHECK(res["id"] == 12345);
        }
    }

    void testAIJsonRpc(const std::string& msg)
    {
        class WalletApiHandler : public WalletApiHandlerBase
        {
        public:
            void onInvalidJsonRpc(const json& msg) override
            {
                WALLET_CHECK(!"invalid asset info api json!!!");
                cout << msg["error"] << endl;
            }

            void onMessage(const JsonRpcId& id, const TxAssetInfo& data) override
            {
                WALLET_CHECK(id > 0);
                WALLET_CHECK((data.assetId && *data.assetId > 0) || (data.assetMeta && !data.assetMeta->empty()));
            }
        };

        WalletApiHandler handler;
        WalletApi api(handler);
        WALLET_CHECK(api.parse(msg.data(), msg.size()));

        {
            json res;
            typename TxAssetInfo::Response status;
            status.txId = { 3,1,3 };
            api.getResponse(12345, status, res);
            testResultHeader(res);

            WALLET_CHECK(res["id"] == 12345);
        }
    }

    void testGetAssetInfoJsonRpc(const std::string& msg)
    {
        class WalletApiHandler : public WalletApiHandlerBase
        {
        public:
            void onInvalidJsonRpc(const json& msg) override
            {
                WALLET_CHECK(!"invalid GetAssetInfo api json!!!");
                cout << msg["error"] << endl;
            }

            void onMessage(const JsonRpcId& id, const GetAssetInfo& data) override
            {
                WALLET_CHECK(id > 0);
                WALLET_CHECK(data.assetId.is_initialized() || data.assetMeta.is_initialized());

                if (data.assetId.is_initialized())
                {
                    const auto assetId = *data.assetId;
                    WALLET_CHECK(assetId > 0);
                }

                if (data.assetMeta.is_initialized())
                {
                    const auto meta = *data.assetMeta;
                    WALLET_CHECK(!meta.empty());
                }
            }
        };

        WalletApiHandler handler;
        WalletApi api(handler);
        WALLET_CHECK(api.parse(msg.data(), msg.size()));

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
        class WalletApiHandler : public WalletApiHandlerBase
        {
        public:

            void onInvalidJsonRpc(const json& msg) override
            {
                WALLET_CHECK(!"invalid status api json!!!");

                cout << msg["error"] << endl;
            }

            void onMessage(const JsonRpcId& id, const Status& data) override
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
                cout << msg["error"] << endl;
            }

            void onMessage(const JsonRpcId& id, const Split& data) override
            {
                WALLET_CHECK(id > 0);

                // WALLET_CHECK(data.session == 123);
                WALLET_CHECK(data.coins[0] == 11);
                WALLET_CHECK(data.coins[1] == 12);
                WALLET_CHECK(data.coins[2] == 13);
                WALLET_CHECK(data.coins[3] == 50000000000000);
                WALLET_CHECK(data.fee == 100);
                WALLET_CHECK(data.assetId && *data.assetId == 1);
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

    void testInvalidSplitJsonRpc(const std::string& msg)
    {
        class WalletApiHandler : public WalletApiHandlerBase
        {
        public:

            void onInvalidJsonRpc(const json& msg) override
            {
                cout << msg["error"] << endl;
            }

            void onMessage(const JsonRpcId& id, const Split& data) override
            {
                WALLET_CHECK(id >= 0);
                WALLET_CHECK(!"error, only onInvalidJsonRpc() should be called!!!");
            }
        };

        WalletApiHandler handler;
        WalletApi api(handler);

        WALLET_CHECK(api.parse(msg.data(), msg.size()));
    }

    void testTxListJsonRpc(const std::string& msg)
    {
        class WalletApiHandler : public WalletApiHandlerBase
        {
        public:

            void onInvalidJsonRpc(const json& msg) override
            {
                WALLET_CHECK(!"invalid list api json!!!");
                cout << msg["error"] << endl;
            }

            void onMessage(const JsonRpcId& id, const TxList& data) override
            {
                WALLET_CHECK(id > 0);
                WALLET_CHECK(*data.filter.status == TxStatus::Completed);
                WALLET_CHECK(data.filter.assetId && *data.filter.assetId == 1);
            }
        };

        WalletApiHandler handler;
        WalletApi api(handler);

        WALLET_CHECK(api.parse(msg.data(), msg.size()));

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
        class WalletApiHandler : public WalletApiHandlerBase
        {
        public:

            void onInvalidJsonRpc(const json& msg) override
            {
                WALLET_CHECK(!"invalid list api json!!!");

                cout << msg["error"] << endl;
            }

            void onMessage(const JsonRpcId& id, const TxList& data) override
            {
                WALLET_CHECK(id > 0);

                WALLET_CHECK(data.skip == 10);
                WALLET_CHECK(data.count == 10);
            }
        };

        WalletApiHandler handler;
        WalletApi api(handler);

        WALLET_CHECK(api.parse(msg.data(), msg.size()));
    }

    void testValidateAddressJsonRpc(const std::string& msg, bool valid)
    {
        class WalletApiHandler : public WalletApiHandlerBase
        {
        public:
            WalletApiHandler(bool valid_) : _valid(valid_)
            {}

            void onInvalidJsonRpc(const json& msg) override
            {
                WALLET_CHECK(!"invalid validate_address api json!!!");

                cout << msg["error"] << endl;
            }

            void onMessage(const JsonRpcId& id, const ValidateAddress& data) override
            {
                WALLET_CHECK(id > 0);
                WALLET_CHECK(data.address.IsValid() == _valid);
            }
        private:
            bool _valid;
        };

        WalletApiHandler handler(valid);
        WalletApi api(handler);

        WALLET_CHECK(api.parse(msg.data(), msg.size()));

        {
            json res;
            ValidateAddress::Response validateResponce;

            validateResponce.isMine = true;
            validateResponce.isValid = valid;

            api.getResponse(123, validateResponce, res);
            testResultHeader(res);

            WALLET_CHECK(res["id"] == 123);
            WALLET_CHECK(res["result"]["is_mine"] == true);
            WALLET_CHECK(res["result"]["is_valid"] == valid);
        }
    }

    void testGenerateTxIdJsonRpc(const std::string& msg)
    {
        class WalletApiHandler : public WalletApiHandlerBase
        {
        public:

            void onInvalidJsonRpc(const json& msg) override
            {
                WALLET_CHECK(!"invalid list api json!!!");

                cout << msg["error"] << endl;
            }

            void onMessage(const JsonRpcId& id, const GenerateTxId& data) override
            {
                WALLET_CHECK(id > 0);
            }
        };

        WalletApiHandler handler;
        WalletApi api(handler);

        WALLET_CHECK(api.parse(msg.data(), msg.size()));

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
        class WalletApiHandler : public WalletApiHandlerBase
        {
        public:

            void onInvalidJsonRpc(const json& msg) override
            {
                WALLET_CHECK(!"invalid list api json!!!");

                cout << msg["error"] << endl;
            }

            void onMessage(const JsonRpcId& id, const ExportPaymentProof& data) override
            {
                WALLET_CHECK(id > 0);
            }
        };

        WalletApiHandler handler;

        WalletApi api(handler);

        WALLET_CHECK(api.parse(msg.data(), msg.size()));

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
        class WalletApiHandler : public WalletApiHandlerBase
        {
        public:

            void onInvalidJsonRpc(const json& msg) override
            {
                WALLET_CHECK(!"invalid list api json!!!");

                cout << msg["error"] << endl;
            }

            void onMessage(const JsonRpcId& id, const VerifyPaymentProof& data) override
            {
                WALLET_CHECK(id > 0);
            }
        };

        WalletApiHandler handler;
        WalletApi api(handler);
        

        WALLET_CHECK(api.parse(msg.data(), msg.size()));

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
        class WalletApiHandler : public WalletApiHandlerBase
        {
        public:

            WalletApiHandler(const T& value) : _value(value) {}

            void onInvalidJsonRpc(const json& msg) override
            {
                WALLET_CHECK(!"invalid api json!!!");
                cout << msg["error"] << endl;
            }

            void onMessage(const JsonRpcId& id, const CreateAddress& data) override
            {
                WALLET_CHECK(id == _value);
            }
        private:
            const T& _value;
        };

        WalletApiHandler handler(value);
        WalletApi api(handler);

        WALLET_CHECK(api.parse(msg.data(), msg.size()));
    }

#ifdef BEAM_ATOMIC_SWAP_SUPPORT
    void testGetBalanceJsonRpc(const std::string& msg)
    {
        class WalletApiHandler : public WalletApiHandlerBase
        {
        public:

            void onInvalidJsonRpc(const json& msg) override
            {
                WALLET_CHECK(!"invalid list api json!!!");

                cout << msg["error"] << endl;
            }

            void onMessage(const JsonRpcId& id, const GetBalance& data) override
            {
                WALLET_CHECK(id > 0);
                WALLET_CHECK(data.coin == AtomicSwapCoin::Litecoin);
            }
        };

        WalletApiHandler handler;
        WalletApi api(handler);


        WALLET_CHECK(api.parse(msg.data(), msg.size()));

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

        class WalletApiHandler : public WalletApiHandlerBase
        {
        public:

            WalletApiHandler(const std::string& value)
                : _value(value)
            {}

            void onInvalidJsonRpc(const json& msg) override
            {
                WALLET_CHECK(!"invalid list api json!!!");

                cout << msg["error"] << endl;
            }

            void onMessage(const JsonRpcId& id, const DecodeToken& data) override
            {
                WALLET_CHECK(id > 0);
                WALLET_CHECK(data.token == _value);
            }

        private:
            std::string _value;
        };

        WalletApiHandler handler(kToken);
        WalletApi api(handler);


        WALLET_CHECK(api.parse(msg.data(), msg.size()));

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

        class WalletApiHandler : public WalletApiHandlerBase
        {
        public:

            WalletApiHandler(const std::string& value)
                : _value(value)
            {}

            void onInvalidJsonRpc(const json& msg) override
            {
                WALLET_CHECK(!"invalid list api json!!!");

                cout << msg["error"] << endl;
            }

            void onMessage(const JsonRpcId& id, const OfferStatus& data) override
            {
                WALLET_CHECK(id > 0);
                WALLET_CHECK(to_hex(data.txId.data(), data.txId.size()) == _value);
            }

        private:
            std::string _value;
        };

        WalletApiHandler handler(kTxId);
        WalletApi api(handler);


        WALLET_CHECK(api.parse(msg.data(), msg.size()));

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
    testInvalidAssetJsonRpc<T>(exp(JSON_CODE(
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
    testInvalidAssetJsonRpc<T>(exp(JSON_CODE(
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
    testInvalidAssetJsonRpc<T>(exp(JSON_CODE(
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
    testInvalidAssetJsonRpc<T>(exp(JSON_CODE(
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
    testInvalidAssetJsonRpc<T>(exp(JSON_CODE(
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
    testInvalidAssetJsonRpc<T>(exp(JSON_CODE(
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
    testInvalidAssetJsonRpc<T>(exp(JSON_CODE(
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
    testInvalidAssetJsonRpc<T>(exp(JSON_CODE(
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
    testInvalidAssetJsonRpc<T>(exp(JSON_CODE(
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
    testInvalidAssetJsonRpc<T>(exp(JSON_CODE(
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
    testInvalidAssetJsonRpc<T>(exp(JSON_CODE(
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
    testInvalidAssetJsonRpc<T>(exp(JSON_CODE(
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
    testInvalidAssetJsonRpc<T>(exp(JSON_CODE(
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

    // valid meta
    testICJsonRpc<T>(exp(JSON_CODE(
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
}

void TestGetAssetInfo()
{
    // Invalid asset id
    testInvalidAssetJsonRpc<GetAssetInfo>(JSON_CODE(
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
    testInvalidAssetJsonRpc<GetAssetInfo>(JSON_CODE(
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
    testInvalidAssetJsonRpc<GetAssetInfo>(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id"     : 12345,
        "method" : "get_asset_info",
        "params" :
        {
        }
    }));

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

    // valid meta
    testGetAssetInfoJsonRpc(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "get_asset_info",
        "params" :
        {
            "asset_meta": "some meta"
        }
    }));
}

void TestAITx()
{
    // Invalid asset id
    testInvalidAssetJsonRpc<TxAssetInfo>(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id"     : 12345,
        "method" : "tx_asset_info",
        "params" :
        {
            "asset_id": -1,
        }
    }));

    // Invalid meta
    testInvalidAssetJsonRpc<TxAssetInfo>(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id"     : 12345,
        "method" : "tx_asset_info",
        "params" :
        {
            "asset_meta": "",
        }
    }));

    // missing asset id & meta
    testInvalidAssetJsonRpc<TxAssetInfo>(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id"     : 12345,
        "method" : "tx_asset_info",
        "params" :
        {
        }
    }));

    // Bad txId (not a hex string)
    testInvalidAssetJsonRpc<TxAssetInfo>(JSON_CODE(
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
    testInvalidAssetJsonRpc<TxAssetInfo>(JSON_CODE(
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

    // valid meta
    testAIJsonRpc(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "tx_asset_info",
        "params" :
        {
            "asset_meta": "some meta"
        }
    }));
}

void TestAssetsAPI()
{
    //
    // EXPLICITLY ENABLE Confidential assets to perform tests
    //
    Rules::get().CA.Enabled = true;
    Rules::get().UpdateChecksum();

    TestICTx<Issue>("tx_asset_issue");
    TestICTx<Consume>("tx_asset_consume");
    TestAITx();
    TestGetAssetInfo();

    Rules::get().CA.Enabled = false;
    Rules::get().UpdateChecksum();
}

int main()
{
    wallet::g_AssetsEnabled = true;

    auto logger = beam::Logger::create();
    testInvalidJsonRpc([](const json& msg)
    {
        testErrorHeader(msg);

        CHECK_JSON_FIELD_ABSENT(msg, "id");
        WALLET_CHECK(msg["error"]["code"] == ApiError::InvalidJsonRpc);
    }, JSON_CODE({}));

    testInvalidJsonRpc([](const json& msg)
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

    testInvalidJsonRpc([](const json& msg)
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

    testInvalidJsonRpc([](const json& msg)
    {
        testErrorHeaderWithId(msg);

        WALLET_CHECK(msg["id"] == 12345);
        WALLET_CHECK(msg["error"]["code"] == ApiError::InvalidJsonRpc);
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
        "method" : "get_utxo",
        "params":
        {
            "filter":
            {
                "asset_id": 1
            }
        }
    }));

    testSendJsonRpc(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "tx_send",
        "params" : 
        {
            "session" : 15,
            "asset_id": 1,
            "value" : 12342342,
            "address" : "472e17b0419055ffee3b3813b98ae671579b0ac0dcd6f1a23b11a75ab148cc67"
        }
    }));

    testInvalidSendJsonRpc(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "tx_send",
        "params" :
        {
            "session" : 15,
            "value" : 12342342,
            "from" : "wagagel",
            "address" : "472e17b0419055ffee3b3813b98ae671579b0ac0dcd6f1a23b11a75ab148cc67"
        }
    }));

    testSendJsonRpc(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "tx_send",
        "params" : 
        {
            "session" : 15,
            "asset_id": 1,
            "value" : 12342342,
            "from" : "19d0adff5f02787819d8df43b442a49b43e72a8b0d04a7cf995237a0422d2be83b6",
            "address" : "472e17b0419055ffee3b3813b98ae671579b0ac0dcd6f1a23b11a75ab148cc67"
        }
    }));

    testJsonRpc<Send>(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "tx_send",
        "params" :
        {
            "session" : 15,
            "asset_id": 1,
            "value" : 1234234200000000000000000000000,
            "from" : "19d0adff5f02787819d8df43b442a49b43e72a8b0d04a7cf995237a0422d2be83b6",
            "address" : "472e17b0419055ffee3b3813b98ae671579b0ac0dcd6f1a23b11a75ab148cc67"
        }
    }), 
    [](const json& msg) {},
    [](const JsonRpcId& id, const Send& data)
    {
        WALLET_CHECK(!"The value is invalid!!!");
    });

    testInvalidSendJsonRpc(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "tx_send",
        "params" :
        {
            "session" : 15,
            "value" : 12342342,
            "from" : "19d0adff5f02787819d8df43b442a49b43e72a8b0d04a7cf995237a0422d2be83b6",
            "address" : "wagagel"
        }
    }));

    // bad asset_id
    testInvalidSendJsonRpc(JSON_CODE({
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "tx_send",
        "params" :
        {
            "session" : 15,
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

    testSplitJsonRpc(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "tx_split",
        "params" :
        {
            "session" : 123,
            "coins" : [11, 12, 13, 50000000000000],
            "fee" : 100,
            "asset_id": 1
        }
    }));

    testInvalidSplitJsonRpc(JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 12345,
        "method" : "tx_split",
        "params" :
        {
            "session" : 123,
            "coins" : [11, -12, 13, 50000000000000] ,
            "fee" : 4
        }
    }));

    testTxListJsonRpc(JSON_CODE(
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

    testInvalidJsonRpc([](const json& msg)
    {
        testErrorHeader(msg);

        CHECK_JSON_FIELD_ABSENT(msg, "id");
        WALLET_CHECK(msg["error"]["code"] == ApiError::InvalidJsonRpc);
    }, JSON_CODE(
    {
        "jsonrpc": "2.0",
        "id" : 1.23
    }));

    testInvalidJsonRpc([](const json& msg)
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

    return WALLET_CHECK_RESULT;
}
