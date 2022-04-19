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
#include "bvm/bvm2.h"

#include <string_view>
#include "utility/common.h"
#include "core/keccak.h"

namespace beam::wallet
{
    namespace
    {
        std::string ToTxHash(const TxID& id)
        {
            std::string r;
            std::string s = std::to_string(id);
            r.append("0x").append(s).append(s);
            return r;
        }
    }
    void V63Api::onHandleChainID(const JsonRpcId& id, ChainID&& req)
    {
        ChainID::Response res;
        doResponse(id, res);
    }

    void V63Api::onHandleNetVersion(const JsonRpcId& id, NetVersion&& req)
    {
        NetVersion::Response res;
        doResponse(id, res);
    }

    void V63Api::onHandleBlockNumber(const JsonRpcId& id, BlockNumber&& req)
    {
        BlockNumber::Response res;

        res.height = get_TipHeight();
        doResponse(id, res);
    }

    void V63Api::onHandleBalance(const JsonRpcId& id, Balance&& req)
    {
        onHandleInvokeContractV61(id, std::move(req.subCall), [this](const auto& id, const auto& response)
            {
                
                Balance::Response res;
                AmountBig::Type b;

                if (response.output)
                {
                    json msg = json::parse(*response.output);
                    if (msg["locked_beam"].is_string())
                    {
                        auto balanceStr = msg["locked_beam"].get<std::string>();
                        b.Scan(balanceStr.data());
                    }
                }
                uintBig_t<sizeof(Amount)> m;
                m = 10'000'000'000UL;
                b = b * m;

                res.balanceHi = AmountBig::get_Hi(b);
                res.balanceLo = AmountBig::get_Lo(b);

                doResponse(id, res);
            });


        
    }

    void V63Api::onHandleBlockByNumber(const JsonRpcId& id, BlockByNumber&& req)
    {
        onHandleBlockDetails(id, std::move(req.subCall), [this](const auto& id, const BlockDetails::Response& responce)
            {
                BlockByNumber::Response res{responce};
                auto walletDB = getWalletDB();
                TxListFilter filter;
                filter.m_KernelProofHeight = responce.height;
                walletDB->visitTx([&res](const TxDescription& tx)
                    {
                        res.txHashes.push_back(ToTxHash(*tx.GetTxID()));
                        return true;
                    }, filter);
                doResponse(id, res);
            });
        
    }

    void V63Api::onHandleGasPrice(const JsonRpcId& id, GasPrice&& req)
    {
        GasPrice::Response res;
        doResponse(id, res);
    }

    void V63Api::onHandleEstimateGas(const JsonRpcId& id, EstimateGas&& req)
    {
        EstimateGas::Response res;
        doResponse(id, res);
    }

    void V63Api::onHandleGetCode(const JsonRpcId& id, GetCode&& req)
    {
        GetCode::Response res;
        doResponse(id, res);
    }

    void V63Api::onHandleGetTransactionCount(const JsonRpcId& id, GetTransactionCount&& req)
    {
        GetTransactionCount::Response res;
        doResponse(id, res);
    }

    void V63Api::onHandleSendRawTransaction(const JsonRpcId& id, SendRawTransaction&& req)
    {
        onHandleInvokeContractV61(id, std::move(req.subCall), [this](const auto& id, const auto& response)
            {
                SendRawTransaction::Response res;
                /*ECC::Hash::Value hv;
                KeccakProcessor<256> hp;
                hp.Write(&*response.txid, sizeof(TxID));
                hp >> hv;*/
                res.txHash = ToTxHash(*response.txid);
                doResponse(id, res);
            });
    }

    void V63Api::onHandleGetTransactionReceipt(const JsonRpcId& id, GetTransactionReceipt&& req)
    {
        auto walletDB = getWalletDB();
        auto tx = walletDB->getTx(req.txID);
        if (!tx)
        {
            GetTransactionReceipt::Response res;
            return doResponse(id, res);
        }
        auto blockNumber = tx->GetParameter<Height>(TxParameterID::KernelProofHeight);
        if (!blockNumber)
        {
            GetTransactionReceipt::Response res;
            return doResponse(id, res);
        }
        onHandleBlockDetails(id, BlockDetails{ *blockNumber }, [this, tx = std::move(tx)](const auto& id, const BlockDetails::Response& responce)
            {
                GetTransactionReceipt::Response res;
                res.txHash = ToTxHash(tx->m_txId);
                res.tx = std::move(tx);
                res.subResponce = responce;
                doResponse(id, res);
            });
    }

    void V63Api::onHandleGetBlockByHash(const JsonRpcId& id, GetBlockByHash&& req)
    {
        GetBlockByHash::Response res;
        auto walletDB = getWalletDB();

        struct Walker :public Block::SystemState::IHistory::IWalker
        {
            Merkle::Hash m_hash;
            Block::SystemState::Full m_state;

            Walker(Merkle::Hash&& h)
                : m_hash(std::move(h))
            {
            }
            bool OnState(const Block::SystemState::Full& s) override
            {
                Merkle::Hash h;
                s.get_Hash(h);

                if (h == m_hash)
                {
                    m_state = s;
                    return false;
                }

                return true;
            }
        } w(Merkle::Hash(res.blockHash));

        walletDB->get_History().Enum(w, nullptr);

        res.number = get_TipHeight();
        doResponse(id, res);
    }

    void V63Api::onHandleCall(const JsonRpcId& id, Call&& req)
    {
        onHandleInvokeContractV61(id, std::move(req.subCall), [this](const auto& id, const auto& response) 
            {
                Call::Response res;
                res.response = std::move(response);
                doResponse(id, res);
            });
    }

    /////


    void V63Api::onHandleIPFSAdd(const JsonRpcId &id, IPFSAdd&& req)
    {
        #ifdef BEAM_IPFS_SUPPORT
        auto ipfs = getIPFS();
        ipfs->AnyThread_add(std::move(req.data), req.pin, req.timeout,
            [this, id, pin = req.pin, wguard = _weakSelf](std::string&& hash) {
                auto guard = wguard.lock();
                if (!guard)
                {
                    LOG_WARNING() << "API destroyed before IPFS response received.";
                    return;
                }

                IPFSAdd::Response response = {hash, pin};
                doResponse(id, response);
            },
            [this, id, wguard = _weakSelf] (std::string&& err) {
                auto guard = wguard.lock();
                if (!guard)
                {
                    LOG_WARNING() << "API destroyed before IPFS response received.";
                    return;
                }

                sendError(id, ApiError::IPFSError, err);
            }
        );
        #else
        sendError(id, ApiError::NotSupported);
        #endif
    }

    void V63Api::onHandleIPFSHash(const JsonRpcId &id, IPFSHash&& req)
    {
        #ifdef BEAM_IPFS_SUPPORT
        auto ipfs = getIPFS();
        ipfs->AnyThread_hash(std::move(req.data), req.timeout,
             [this, id, wguard = _weakSelf](std::string&& hash) {
                 auto guard = wguard.lock();
                 if (!guard)
                 {
                     LOG_WARNING() << "API destroyed before IPFS response received.";
                     return;
                 }

                 IPFSHash::Response response = {hash};
                 doResponse(id, response);
             },
             [this, id, wguard = _weakSelf] (std::string&& err) {
                 auto guard = wguard.lock();
                 if (!guard)
                 {
                     LOG_WARNING() << "API destroyed before IPFS response received.";
                     return;
                 }

                 sendError(id, ApiError::IPFSError, err);
             }
        );
        #else
        sendError(id, ApiError::NotSupported);
        #endif
    }

    void V63Api::onHandleIPFSGet(const JsonRpcId &id, IPFSGet&& req)
    {
        #ifdef BEAM_IPFS_SUPPORT
        auto ipfs = getIPFS();
        ipfs->AnyThread_get(req.hash, req.timeout,
        [this, id, hash = req.hash, wguard = _weakSelf](std::vector<uint8_t>&& data) {
                auto guard = wguard.lock();
                if (!guard)
                {
                    LOG_WARNING() << "API destroyed before IPFS response received.";
                    return;
                }

                IPFSGet::Response response = {hash, std::move(data)};
                doResponse(id, response);
            },
            [this, id, wguard = _weakSelf] (std::string&& err) {
                auto guard = wguard.lock();
                if (!guard)
                {
                    LOG_WARNING() << "API destroyed before IPFS response received.";
                    return;
                }

                sendError(id, ApiError::IPFSError, err);
            }
        );
        #else
        sendError(id, ApiError::NotSupported);
        #endif
    }

    void V63Api::onHandleIPFSPin(const JsonRpcId &id, IPFSPin&& req)
    {
        #ifdef BEAM_IPFS_SUPPORT
        auto ipfs = getIPFS();
        ipfs->AnyThread_pin(req.hash, req.timeout,
        [this, id, hash = req.hash, wguard = _weakSelf]() {
                auto guard = wguard.lock();
                if (!guard)
                {
                    LOG_WARNING() << "API destroyed before IPFS response received.";
                    return;
                }

                IPFSPin::Response response = {hash};
                doResponse(id, response);
            },
            [this, id, wguard = _weakSelf] (std::string&& err) {
                auto guard = wguard.lock();
                if (!guard)
                {
                    LOG_WARNING() << "API destroyed before IPFS response received.";
                    return;
                }
                sendError(id, ApiError::IPFSError, err);
            }
        );
        #else
        sendError(id, ApiError::NotSupported);
        #endif
    }

    void V63Api::onHandleIPFSUnpin(const JsonRpcId &id, IPFSUnpin&& req)
    {
        #ifdef BEAM_IPFS_SUPPORT
        auto ipfs = getIPFS();
        ipfs->AnyThread_unpin(req.hash,
          [this, id, hash = req.hash, wguard = _weakSelf]() {
              auto guard = wguard.lock();
              if (!guard)
              {
                  LOG_WARNING() << "API destroyed before IPFS response received.";
                  return;
              }

              IPFSUnpin::Response response = {hash};
              doResponse(id, response);
          },
          [this, id, wguard = _weakSelf] (std::string&& err) {
              auto guard = wguard.lock();
              if (!guard)
              {
                  LOG_WARNING() << "API destroyed before IPFS response received.";
                  return;
              }
              sendError(id, ApiError::IPFSError, err);
          }
        );
        #else
        sendError(id, ApiError::NotSupported);
        #endif
    }

    void V63Api::onHandleIPFSGc(const JsonRpcId &id, IPFSGc&& req)
    {
        #ifdef BEAM_IPFS_SUPPORT
        auto ipfs = getIPFS();
        ipfs->AnyThread_gc(req.timeout,
            [this, id, wguard = _weakSelf]() {
                auto guard = wguard.lock();
                if (!guard)
                {
                    LOG_WARNING() << "API destroyed before IPFS response received.";
                    return;
                }

                IPFSGc::Response response;
                doResponse(id, response);
            },
            [this, id, wguard = _weakSelf] (std::string&& err) {
                auto guard = wguard.lock();
                if (!guard)
                {
                    LOG_WARNING() << "API destroyed before IPFS response received.";
                    return;
                }
                sendError(id, ApiError::IPFSError, err);
            }
        );
        #else
        sendError(id, ApiError::NotSupported);
        #endif
    }

    namespace
    {
        struct MyProcessor : bvm2::ProcessorManager
        {
            using ProcessorManager::DeriveKeyPreimage;
        };

        void GetMessageHash(ECC::Hash::Value& hv, const std::string& message)
        {
            std::stringstream ss;
            ss << "beam.signed.message"
                << message.size()
                << message;

            ECC::Hash::Processor()
                << ss.str()
                >> hv;
        }
    }

    void V63Api::onHandleSignMessage(const JsonRpcId& id, SignMessage&& req)
    {
        SignMessage::Response resp;
        ECC::Hash::Value hv;
        MyProcessor::DeriveKeyPreimage(hv, Blob(req.keyMaterial));
        auto db = getWalletDB();
        auto pKdf = db->get_MasterKdf();
        ECC::Scalar::Native sk;
        pKdf->DeriveKey(sk, hv);

        GetMessageHash(hv, req.message);

        ECC::Signature sig;
        sig.Sign(hv, sk);

        Serializer s;
        s & sig;

        resp.signature = to_hex(s.buffer().first, s.buffer().second);
        doResponse(id, resp);
    }

    void V63Api::onHandleVerifySignature(const JsonRpcId& id, VerifySignature&& req)
    {
        VerifySignature::Response resp;
        ECC::Hash::Value hv;
        GetMessageHash(hv, req.message);

        Deserializer d;
        ECC::Signature sig;
        d.reset(req.signature);
        d& sig;
        resp.result = sig.IsValid(hv, req.publicKey);
        doResponse(id, resp);
    }
}
