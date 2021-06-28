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
#include "v6_1_api.h"
#include "version.h"

namespace beam::wallet
{
    void V61Api::onHandleEvSubscribe(const JsonRpcId &id, const EvSubscribe &data)
    {
        if(_evSubscribed)
        {
            EvSubscribe::Response resp{true};
            return doResponse(id, resp);
        }

        getWallet()->Subscribe(this);
        _evSubscribed = true;

        EvSubscribe::Response resp{true};
        doResponse(id, resp);
    }

    void V61Api::onHandleEvUnsubscribe(const JsonRpcId& id, const EvUnsubscribe& params)
    {
        if (!_evSubscribed)
        {
            EvUnsubscribe::Response resp{true};
            return doResponse(id, resp);
        }

        getWallet()->Unsubscribe(this);
        _evSubscribed = false;

        EvUnsubscribe::Response resp{true};
        doResponse(id, resp);
    }

    void V61Api::onHandleGetVersion(const JsonRpcId& id, const GetVersion& params)
    {
        GetVersion::Response resp;

        resp.apiVersion          = _apiVersion;
        resp.apiVersionMajor     = _apiVersionMajor;
        resp.apiVersionMinor     = _apiVersionMinor;
        resp.beamVersion         = PROJECT_VERSION;
        resp.beamVersionMajor    = VERSION_MAJOR;
        resp.beamVersionMinor    = VERSION_MINOR;
        resp.beamVersionRevision = VERSION_REVISION;
        resp.beamCommitHash      = GIT_COMMIT_HASH;
        resp.beamBranchName      = BRANCH_NAME;

        doResponse(id, resp);
    }
}
