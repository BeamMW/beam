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

#include <string_view>
#include "utility/common.h"

namespace beam::wallet
{
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
        Balance::Response res;
        auto walletDB = getWalletDB();
        storage::Totals allTotals(*walletDB, false);
        const auto& totals = allTotals.GetBeamTotals();
        auto b = totals.Avail;
        b += totals.AvailShielded;
        uintBig_t<sizeof(Amount)> m;
        m = 10'000'000'000UL;
        b = b * m ;
        
        res.balanceHi = AmountBig::get_Hi(b);
        res.balanceLo = AmountBig::get_Lo(b);

        doResponse(id, res);
    }

    void V63Api::onHandleBlockByNumber(const JsonRpcId& id, BlockByNumber&& req)
    {
        BlockByNumber::Response res;
        res.number = get_TipHeight();
        doResponse(id, res);
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
                //res.response = std::move(response);
                doResponse(id, res);
            });
    }

    void V63Api::onHandleGetTransactionReceipt(const JsonRpcId& id, GetTransactionReceipt&& req)
    {
        GetTransactionReceipt::Response res;
        doResponse(id, res);
    }

    void V63Api::onHandleGetBlockByHash(const JsonRpcId& id, GetBlockByHash&& req)
    {
        GetBlockByHash::Response res;
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
}
