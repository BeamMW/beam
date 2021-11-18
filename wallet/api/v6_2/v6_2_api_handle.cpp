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
#include "v6_2_api.h"
#include "version.h"

namespace beam::wallet
{
    void V62Api::onHandleIPFSAdd(const JsonRpcId &id, const IPFSAdd& req)
    {
        auto ipfs = getIPFS();

        // TODO:IPFS cannot use move because of const
        std::vector dcopy = req.data;
        ipfs->add(std::move(dcopy),
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
    }

    void V62Api::onHandleIPFSGet(const JsonRpcId &id, const IPFSGet& req)
    {
        auto ipfs = getIPFS();
        ipfs->get(req.hash,
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
    }
}
