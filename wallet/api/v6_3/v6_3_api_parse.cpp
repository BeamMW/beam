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
#include "v6_3_api.h"
#include "version.h"

namespace beam::wallet
{
    namespace
    {
        uint32_t parseTimeout(V63Api& api, const nlohmann::json& params)
        {
            if(auto otimeout = api.getOptionalParam<uint32_t>(params, "timeout"))
            {
                return *otimeout;
            }
            return 0;
        }
    }

    std::pair<ChainID, IWalletApi::MethodInfo> V63Api::onParseChainID(const JsonRpcId& id, const nlohmann::json& params)
    {
        ChainID message;
        return std::make_pair(message, MethodInfo());
    }

    void V63Api::getResponse(const JsonRpcId& id, const ChainID::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", "0x582"}
        };
    }

    std::pair<NetVersion, IWalletApi::MethodInfo> V63Api::onParseNetVersion(const JsonRpcId& id, const nlohmann::json& params)
    {
        NetVersion message;
        return std::make_pair(message, MethodInfo());
    }

    void V63Api::getResponse(const JsonRpcId& id, const NetVersion::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", "0x582"}
        };
    }

    std::pair<BlockNumber, IWalletApi::MethodInfo> V63Api::onParseBlockNumber(const JsonRpcId& id, const nlohmann::json& params)
    {
        BlockNumber message;
        return std::make_pair(message, MethodInfo());
    }

    void V63Api::getResponse(const JsonRpcId& id, const BlockNumber::Response& res, json& msg)
    {
        std::stringstream ss;
        ss << std::hex << std::showbase << res.height;
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", ss.str()}
        };
    }

    std::pair<Balance, IWalletApi::MethodInfo> V63Api::onParseBalance(const JsonRpcId& id, const nlohmann::json& params)
    {
        Balance message;
        message.address = params[0].get<std::string>();
        auto t = params[1].get<std::string>();
        message.tag = t;
        return std::make_pair(message, MethodInfo());
    }

    void V63Api::getResponse(const JsonRpcId& id, const Balance::Response& res, json& msg)
    {
        std::stringstream ss;
        ss << std::hex << std::showbase << res.balanceHi << std::noshowbase << res.balanceLo;
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", ss.str()}
        };
    }

    std::pair<BlockByNumber, IWalletApi::MethodInfo> V63Api::onParseBlockByNumber(const JsonRpcId& id, const nlohmann::json& params)
    {
        BlockByNumber message;
        message.tag = params[0].get<std::string>();
        message.fullTxInfo = params[1].get<bool>();
        return std::make_pair(message, MethodInfo());
    }

    void V63Api::getResponse(const JsonRpcId& id, const BlockByNumber::Response& res, json& msg)
    {
        std::stringstream ss;
        ss << std::hex << std::showbase << res.number;
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", 
                {
                    {"difficulty", "0x4ea3f27bc"},
                    {"extraData", "0x476574682f4c5649562f76312e302e302f6c696e75782f676f312e342e32"},
                    {"gasLimit", "0x1388"},
                    {"gasUsed", "0x0"},
                    {"hash", "0xdc0818cf78f21a8e70579cb46a43643f78291264dda342ae31049421c82d21ae"},
                    {"logsBloom", "0x00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"},
                    {"miner", "0xbb7b8287f3f0a933474a79eae42cbca977791171"},
                    {"mixHash", "0x4fffe9ae21f1c9e15207b1f472d5bbdd68c9595d461666602f2be20daf5e7843"},
                    {"nonce", "0x689056015818adbe"},
                    {"number", ss.str()},
                    {"parentHash", "0xe99e022112df268087ea7eafaf4790497fd21dbeeb6bd7a1721df161a6657a54"},
                    {"receiptsRoot", "0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421"},
                    {"sha3Uncles", "0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347"},
                    {"size", "0x220"},
                    {"stateRoot", "0xddc8b0234c2e0cad087c8b389aa7ef01f7d79b2570bccb77ce48648aa61c904d"},
                    {"timestamp", "0x55ba467c"},
                    {"totalDifficulty", "0x78ed983323d"},
                    {"transactions", json::array()} ,
                    {"transactionsRoot", "0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421"},
                    {"uncles", json::array() }
                }
            }

        };
    }

    std::pair<GasPrice, IWalletApi::MethodInfo> V63Api::onParseGasPrice(const JsonRpcId& id, const nlohmann::json& params)
    {
        GasPrice message;
        return std::make_pair(message, MethodInfo());
    }

    void V63Api::getResponse(const JsonRpcId& id, const GasPrice::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", "0x0"}

        };
    }

    std::pair<EstimateGas, IWalletApi::MethodInfo> V63Api::onParseEstimateGas(const JsonRpcId& id, const nlohmann::json& params)
    {
        EstimateGas message;
        return std::make_pair(message, MethodInfo());
    }

    void V63Api::getResponse(const JsonRpcId& id, const EstimateGas::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", "0x0"}

        };
    }

    std::pair<GetCode, IWalletApi::MethodInfo> V63Api::onParseGetCode(const JsonRpcId& id, const nlohmann::json& params)
    {
        GetCode message;
        return std::make_pair(message, MethodInfo());
    }

    void V63Api::getResponse(const JsonRpcId& id, const GetCode::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", "0x600160008035811a818181146012578301005b601b6001356025565b8060005260206000f25b600060078202905091905056"}

        };
    }

    std::pair<GetTransactionCount, IWalletApi::MethodInfo> V63Api::onParseGetTransactionCount(const JsonRpcId& id, const nlohmann::json& params)
    {
        GetTransactionCount message;
        return std::make_pair(message, MethodInfo());
    }

    void V63Api::getResponse(const JsonRpcId& id, const GetTransactionCount::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", "0x1"}

        };
    }

    std::pair<SendRawTransaction, IWalletApi::MethodInfo> V63Api::onParseSendRawTransaction(const JsonRpcId& id, const nlohmann::json& params)
    {
        SendRawTransaction message;
        auto data = params[0].get<std::string>();
        auto pos = data.find_first_not_of("0x");
        data = data.substr(std::min(pos, data.size()));
        message.rawTransaction = from_hex(data);
        return std::make_pair(message, MethodInfo());
    }

    void V63Api::getResponse(const JsonRpcId& id, const SendRawTransaction::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", "0xe670ec64341771606e55d6b4ca35a1a6b75ee3d5145a99d05921026d1527331"}

        };
    }

    std::pair<GetTransactionReceipt, IWalletApi::MethodInfo> V63Api::onParseGetTransactionReceipt(const JsonRpcId& id, const nlohmann::json& params)
    {
        GetTransactionReceipt message;
        return std::make_pair(message, MethodInfo());
    }

    void V63Api::getResponse(const JsonRpcId& id, const GetTransactionReceipt::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", 
                {
                    {"transactionHash", "0xe670ec64341771606e55d6b4ca35a1a6b75ee3d5145a99d05921026d1527331"},
                    {"transactionIndex", "0x1"}, // 1
                    {"blockNumber", "0xb"}, // 11
                    {"blockHash", "0xc6ef2fc5426d6ad6fd9e2a26abeab0aa2411b7ab17f30a99d3cb96aed1d1055b"},
                    {"cumulativeGasUsed", "0x33bc"}, // 13244
                    {"gasUsed", "0x0"}, // 1244
                    {"contractAddress", "0x3bb7488199eA33F05336729D0f57129A801Fd0b9"}, // or null, if none was created
                    //{"logs : [{
                    //{"       // logs as returned by getFilterLogs, etc.
                    //{"   }, ...] ,
                    //{"logsBloom : "0x00...0", // 256 byte bloom filter
                    {"status", "0x1"}
                }
            }

        };
    }

    std::pair<GetBlockByHash, IWalletApi::MethodInfo> V63Api::onParseGetBlockByHash(const JsonRpcId& id, const nlohmann::json& params)
    {
        GetBlockByHash message;
        return std::make_pair(message, MethodInfo());
    }

    void V63Api::getResponse(const JsonRpcId& id, const GetBlockByHash::Response& res, json& msg)
    {
        std::stringstream ss;
        ss << std::hex << std::showbase << res.number;
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result",
                {
                    {"difficulty", "0x4ea3f27bc"},
                    {"extraData", "0x476574682f4c5649562f76312e302e302f6c696e75782f676f312e342e32"},
                    {"gasLimit", "0x1388"},
                    {"gasUsed", "0x0"},
                    {"hash", "0xc6ef2fc5426d6ad6fd9e2a26abeab0aa2411b7ab17f30a99d3cb96aed1d1055b"},
                    {"logsBloom", "0x00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"},
                    {"miner", "0xbb7b8287f3f0a933474a79eae42cbca977791171"},
                    {"mixHash", "0x4fffe9ae21f1c9e15207b1f472d5bbdd68c9595d461666602f2be20daf5e7843"},
                    {"nonce", "0x689056015818adbe"},
                    {"number", ss.str()},
                    {"parentHash", "0xe99e022112df268087ea7eafaf4790497fd21dbeeb6bd7a1721df161a6657a54"},
                    {"receiptsRoot", "0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421"},
                    {"sha3Uncles", "0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347"},
                    {"size", "0x220"},
                    {"stateRoot", "0xddc8b0234c2e0cad087c8b389aa7ef01f7d79b2570bccb77ce48648aa61c904d"},
                    {"timestamp", "0x55ba467c"},
                    {"totalDifficulty", "0x78ed983323d"},
                    {"transactions", 
                        {
                            "0xe670ec64341771606e55d6b4ca35a1a6b75ee3d5145a99d05921026d1527331"
                        }
                    } ,
                    {"transactionsRoot", "0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421"},
                    {"uncles", json::array() }
                }
            }

        };

    }

    

    /////////////////////////////


    std::pair<IPFSAdd, IWalletApi::MethodInfo> V63Api::onParseIPFSAdd(const JsonRpcId& id, const nlohmann::json& params)
    {
        IPFSAdd message;
        message.timeout = parseTimeout(*this, params);

        json data = getMandatoryParam<NonEmptyJsonArray>(params, "data");
        data.get<std::vector<uint8_t>>().swap(message.data);

        if (auto opin = getOptionalParam<bool>(params, "pin"))
        {
            message.pin = *opin;
        }

        return std::make_pair(std::move(message), MethodInfo());
    }

    void V63Api::getResponse(const JsonRpcId& id, const IPFSAdd::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result",
                {
                    {"hash", res.hash},
                    {"pinned", res.pinned}
                }
            }
        };
    }

    std::pair<IPFSHash, IWalletApi::MethodInfo> V63Api::onParseIPFSHash(const JsonRpcId& id, const nlohmann::json& params)
    {
        IPFSHash message;
        message.timeout = parseTimeout(*this, params);

        json data = getMandatoryParam<NonEmptyJsonArray>(params, "data");
        data.get<std::vector<uint8_t>>().swap(message.data);

        return std::make_pair(std::move(message), MethodInfo());
    }

    void V63Api::getResponse(const JsonRpcId& id, const IPFSHash::Response& res, json& msg)
    {
        msg = json
            {
                {JsonRpcHeader, JsonRpcVersion},
                {"id", id},
                {"result",
                    {
                        {"hash", res.hash}
                    }
                }
            };
    }

    std::pair<IPFSGet, IWalletApi::MethodInfo> V63Api::onParseIPFSGet(const JsonRpcId& id, const nlohmann::json& params)
    {
        IPFSGet message;
        message.timeout = parseTimeout(*this, params);
        message.hash = getMandatoryParam<NonEmptyString>(params, "hash");
        return std::make_pair(std::move(message), MethodInfo());
    }

    void V63Api::getResponse(const JsonRpcId& id, const IPFSGet::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result",
                {
                    {"hash", res.hash},
                    {"data", res.data}
                }
            }
        };
    }

    std::pair<IPFSPin, IWalletApi::MethodInfo> V63Api::onParseIPFSPin(const JsonRpcId& id, const nlohmann::json& params)
    {
        IPFSPin message;
        message.timeout = parseTimeout(*this, params);
        message.hash = getMandatoryParam<NonEmptyString>(params, "hash");
        return std::make_pair(std::move(message), MethodInfo());
    }

    void V63Api::getResponse(const JsonRpcId& id, const IPFSPin::Response& res, json& msg)
    {
        msg = json
            {
                {JsonRpcHeader, JsonRpcVersion},
                {"id", id},
                {"result",
                    {
                        {"hash", res.hash}
                    }
                }
            };
    }

    std::pair<IPFSUnpin, IWalletApi::MethodInfo> V63Api::onParseIPFSUnpin(const JsonRpcId& id, const nlohmann::json& params)
    {
        IPFSUnpin message;
        message.hash = getMandatoryParam<NonEmptyString>(params, "hash");
        return std::make_pair(std::move(message), MethodInfo());
    }

    void V63Api::getResponse(const JsonRpcId& id, const IPFSUnpin::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result",
                {
                    {"hash", res.hash}
                }
            }
        };
    }

    std::pair<IPFSGc, IWalletApi::MethodInfo> V63Api::onParseIPFSGc(const JsonRpcId& id, const nlohmann::json& params)
    {
        IPFSGc message;
        message.timeout = parseTimeout(*this, params);
        return std::make_pair(message, MethodInfo());
    }

    void V63Api::getResponse(const JsonRpcId& id, const IPFSGc::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result",
                {
                    {"result", true}
                }
            }
        };
    }
}
