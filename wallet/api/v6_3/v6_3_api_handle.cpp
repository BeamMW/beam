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
    void V63Api::onHandleIPFSAdd(const JsonRpcId &id, IPFSAdd&& req)
    {
        #ifdef BEAM_IPFS_SUPPORT
        auto ipfs = getIPFS();
        ipfs->AnyThread_add(std::move(req.data),
            [this, id, wguard = _weakSelf](std::string&& hash) {
                auto guard = wguard.lock();
                if (!guard)
                {
                    LOG_WARNING() << "API destroyed before IPFS response received.";
                    return;
                }

                IPFSAdd::Response response = {hash};
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
        // TODO:IPFS access via http
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
        ipfs->AnyThread_unpin(req.hash, req.timeout,
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
