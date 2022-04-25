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

namespace beam::wallet
{
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
