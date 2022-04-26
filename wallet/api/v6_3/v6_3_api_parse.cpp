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
#define HOST_BUILD
#include "v6_3_api.h"
#include "wallet/api/v6_1/v6_1_api.h"
#include "version.h"

#ifdef BEAM_ETH_API_EMULATION

#define BEAM_SHADERS_USE_LIBBITCOIN
#define BEAM_SHADERS_USE_STL
#include "bvm/bvm2_impl.h"
#include "bitcoin/bitcoin/math/elliptic_curve.hpp"
#include <ethash/keccak.hpp>
#include "bvm/Shaders/Eth.h"

#endif // BEAM_ETH_API_EMULATION

namespace beam::wallet
{
    namespace
    {
        uint32_t parseTimeout(V63Api& api, const nlohmann::json& params)
        {
            if (auto otimeout = api.getOptionalParam<uint32_t>(params, "timeout"))
            {
                return *otimeout;
            }
            return 0;
        }

        bool ExtractPoint(ECC::Point::Native& point, const json& j)
        {
            std::string s = type_get<NonEmptyString>(j);
            auto buf = from_hex(s);
            ECC::Point pt;
            Deserializer dr;
            dr.reset(buf);
            dr& pt;

            return point.ImportNnz(pt);
        }
    }

    template<>
    const char* type_name<ECC::Point::Native>()
    {
        return "hex encoded elliptic curve point";
    }

    template<>
    bool type_check<ECC::Point::Native>(const json& j)
    {
        ECC::Point::Native pt;
        return type_check<NonEmptyString>(j) && ExtractPoint(pt, j);
    }

    template<>
    ECC::Point::Native type_get<ECC::Point::Native>(const json& j)
    {
        ECC::Point::Native pt;
        ExtractPoint(pt, j);
        return pt;
    }

#ifdef BEAM_ETH_API_EMULATION
    namespace
    {
        template<typename T>
        std::string ToHex(T& v)
        {
            std::stringstream ss;
            ss << std::hex << std::showbase << v;
            return ss.str();
        }

        template<typename T, typename ...Argv>
        std::string ToHex(const T& v0, Argv... v)
        {
            std::stringstream ss;
            ((ss << std::hex << std::showbase << v0 << std::noshowbase) << ... << v);
            return ss.str();
        }

        ByteBuffer FromHex(std::string s)
        {
            std::string_view sv(s.data(), s.size());
            auto pos = sv.find("0x");
            sv.remove_prefix(pos == 0 ? 2 : 0);
            return from_hex(sv);
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
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", ToHex(res.height)}
        };
    }

    std::pair<Balance, IWalletApi::MethodInfo> V63Api::onParseBalance(const JsonRpcId& id, const nlohmann::json& params)
    {
        Balance message;
        message.address = params[0].get<std::string>();
        auto t = params[1].get<std::string>();
        message.tag = t;
        message.subCall.createTx = false;
        message.subCall.args.append("role=user,action=view_account,accountID=")
                            .append(message.address.substr(2))
                            .append(",cid=38a60c284e4c81f9c09e6a5b042ca4654730c6717768edada343bca4bf8cbdd7");

        return std::make_pair(message, MethodInfo());
    }

    void V63Api::getResponse(const JsonRpcId& id, const Balance::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", ToHex(res.balanceHi, res.balanceLo)}
        };
    }

    std::pair<BlockByNumber, IWalletApi::MethodInfo> V63Api::onParseBlockByNumber(const JsonRpcId& id, const nlohmann::json& params)
    {
        BlockByNumber message;
        message.tag = params[0].get<std::string>();
        message.fullTxInfo = params[1].get<bool>();
        auto b = from_hex(std::string_view(message.tag.data(), message.tag.size()).substr(2));
        Height height = 0;
        for (uint8_t c : b)
        {
            height <<= 8;
            height += c;
        }

        message.subCall.blockHeight = height;
        return std::make_pair(message, MethodInfo());
    }

    void V63Api::getResponse(const JsonRpcId& id, const BlockByNumber::Response& res, json& msg)
    {
        FillBlockResponse(id, res.subResponce, res.txHashes, msg);
    }

    std::pair<GetBlockByHash, IWalletApi::MethodInfo> V63Api::onParseGetBlockByHash(const JsonRpcId& id, const nlohmann::json& params)
    {
        GetBlockByHash message;
        message.blockHash = FromHex(params[0].get<std::string>());
        return std::make_pair(message, MethodInfo());
    }

    void V63Api::FillBlockResponse(const JsonRpcId& id, const BlockDetails::Response& res, const std::vector<std::string>& txHashes, json& msg) const
    {
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
                    {"hash", "0x" + res.blockHash},
                    {"logsBloom", "0x00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"},
                    {"miner", "0xbb7b8287f3f0a933474a79eae42cbca977791171"},
                    {"mixHash", "0x4fffe9ae21f1c9e15207b1f472d5bbdd68c9595d461666602f2be20daf5e7843"},
                    {"nonce", "0x689056015818adbe"},
                    {"number", ToHex(res.height)},
                    {"parentHash", "0xe99e022112df268087ea7eafaf4790497fd21dbeeb6bd7a1721df161a6657a54"},
                    {"receiptsRoot", "0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421"},
                    {"sha3Uncles", "0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347"},
                    {"size", "0x220"},
                    {"stateRoot", "0xddc8b0234c2e0cad087c8b389aa7ef01f7d79b2570bccb77ce48648aa61c904d"},
                    {"timestamp", ToHex(res.timestamp)},
                    {"totalDifficulty", "0x78ed983323d"},
                    { "transactions", json::array() },
                    { "transactionsRoot", "0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421" },
                    { "uncles", json::array() }
                }
            }
        };
        for (const auto& tx : txHashes)
        {
            msg["result"]["transactions"].push_back(tx);
        }
    }

    void V63Api::getResponse(const JsonRpcId& id, const GetBlockByHash::Response& res, json& msg)
    {
        FillBlockResponse(id, res.subResponce, res.txHashes, msg);
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
        using namespace Shaders::Eth;
        SendRawTransaction message;
        auto data = params[0].get<std::string>();
        auto pos = data.find("0x");
        if (pos == 0)
        {
            data = data.substr(2);
        }
        auto buf = from_hex(data);

        RawTransactionData tx;
        if (!ExtractDataFromRawTransaction(tx, buf.data(), buf.size()))
        {
            throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, "Invalid transaction data");
        }

        message.subCall.createTx = true;
        message.subCall.args.assign(reinterpret_cast<const char*>(tx.data.data()), tx.data.size());
        message.subCall.args.append(",data=")
                            .append(data);

        Shaders::Eth::PublicKey pubKey;
        libbitcoin::ec_uncompressed upub;
        if (!ExtractPubKeyFromSignature(pubKey, tx.messageHash, tx.signature, tx.recoveryID) ||
            !libbitcoin::decompress(upub, *reinterpret_cast<const libbitcoin::ec_compressed*>(&pubKey)))
        {
            throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, "Failed to extract public key");
        }

        message.subCall.args.append(",accountY=")
                            .append(to_hex(&upub[33], 32));

        return std::make_pair(message, MethodInfo());
    }

    void V63Api::getResponse(const JsonRpcId& id, const SendRawTransaction::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", res.txHash}

        };
    }

    std::pair<GetTransactionReceipt, IWalletApi::MethodInfo> V63Api::onParseGetTransactionReceipt(const JsonRpcId& id, const nlohmann::json& params)
    {
        GetTransactionReceipt message;
        auto b = FromHex(params[0].get<std::string>());
        std::copy_n(begin(b), sizeof(TxID), begin(message.txID));
        return std::make_pair(message, MethodInfo());
    }

    void V63Api::getResponse(const JsonRpcId& id, const GetTransactionReceipt::Response& res, json& msg)
    {
        if (!res.tx)
        {
            msg = json
            {
                {JsonRpcHeader, JsonRpcVersion},
                {"id", id},
                {"result", "null"}
            };
            return;
        }
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", 
                {
                    {"transactionHash", res.txHash},
                    {"transactionIndex", "0x1"}, // 1
                    {"blockNumber", ToHex(res.subResponce.height)}, // 11
                    {"blockHash", "0x" + res.subResponce.blockHash},
                    {"cumulativeGasUsed", "0x33bc"}, // 13244
                    {"gasUsed", "0x0"}, // 1244
                    //{"contractAddress", "0x3bb7488199eA33F05336729D0f57129A801Fd0b9"}, // or null, if none was created
                    //{"logs : [{
                    //{"       // logs as returned by getFilterLogs, etc.
                    //{"   }, ...] ,
                    //{"logsBloom : "0x00...0", // 256 byte bloom filter
                    {"status", "0x1"}
                }
            }

        };
    }

    std::pair<Call, IWalletApi::MethodInfo> V63Api::onParseCall(const JsonRpcId& id, const nlohmann::json& params)
    {
        Call message;
 
        std::string data = getMandatoryParam<NonEmptyString>(params[0], "data");
        auto subcallInfo = parseCallInfo(data.data(), data.size());
        
        if (!subcallInfo || subcallInfo->method != "invoke_contract")
        {
            throw jsonrpc_exception(ApiError::InvalidParamsJsonRpc, "Data is not API call");
        }
        
        auto parseRes = onParseInvokeContractV61(subcallInfo->rpcid, subcallInfo->params);

        message.subCall = parseRes.first;

        return std::make_pair(message, parseRes.second);
    }

    void V63Api::getResponse(const JsonRpcId& id, const Call::Response& res, json& msg)
    {
        json subRes;

        V61Api::getResponse(id, res.response, subRes);

        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", subRes.dump()}
        };
    }

#endif // BEAM_ETH_API_EMULATION

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

    std::pair<SignMessage, IWalletApi::MethodInfo> V63Api::onParseSignMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        SignMessage message;
        message.message = getMandatoryParam<NonEmptyString>(params, "message");
        auto km = getMandatoryParam<NonEmptyString>(params, "key_material");
        message.keyMaterial = from_hex_string(km);
        return std::make_pair(message, MethodInfo());
    }

    void V63Api::getResponse(const JsonRpcId& id, const SignMessage::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result",
                {
                    {"signature", res.signature}
                }
            }
        };
    }

    std::pair<VerifySignature, IWalletApi::MethodInfo> V63Api::onParseVerifySignature(const JsonRpcId& id, const nlohmann::json& params)
    {
        VerifySignature message;
        message.message = getMandatoryParam<NonEmptyString>(params, "message");
        message.publicKey = getMandatoryParam<ECC::Point::Native>(params, "public_key");
        message.signature = getMandatoryParam<ValidHexBuffer>(params, "signature");
        
        return std::make_pair(message, MethodInfo());
    }

    void V63Api::getResponse(const JsonRpcId& id, const VerifySignature::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHeader, JsonRpcVersion},
            {"id", id},
            {"result", res.result }
        };
    }
}
