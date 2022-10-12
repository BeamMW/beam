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
#include "v7_1_api.h"
#include "version.h"
#include "bvm/bvm2.h"

namespace beam::wallet
{
    void V71Api::onHandleDeriveID(const JsonRpcId& id, DeriveID&& req)
    {
        ECC::Scalar s;
        ECC::Hash::Processor()
            << "api.unique"
            << Blob(req.tag.c_str(), (uint32_t) req.tag.size())
            >> s.m_Value;

        auto pKdf = getWalletDB()->get_OwnerKdf();
        ECC::Scalar::Native sk;
        pKdf->DerivePKey(sk, s.m_Value);
        s = sk;

        DeriveID::Response resp;
        resp.hash.resize(s.m_Value.nTxtLen);
        s.m_Value.Print(&resp.hash.front());

        doResponse(id, resp);
    }
}
