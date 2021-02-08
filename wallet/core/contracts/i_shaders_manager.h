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
#pragma once

#include "bvm/ManagerStd.h"
#include "wallet/core/wallet_db.h"
#include "wallet/core/wallet.h"

namespace beam::wallet
{
    class IShadersManager
    {
    public:
        struct IDone {
            typedef decltype(beam::bvm2::ManagerStd::m_vInvokeData) InvokeData;
            virtual void onShaderDone(
                    boost::optional<TxID> txid,
                    boost::optional<std::string> result,
                    boost::optional<std::string> error) = 0;
        };

        typedef std::shared_ptr<IShadersManager> Ptr;
        typedef std::weak_ptr<IShadersManager> WeakPtr;

        static Ptr CreateInstance(
                beam::wallet::Wallet::Ptr wallet,
                beam::wallet::IWalletDB::Ptr wdb,
                beam::proto::FlyClient::INetwork::Ptr nodeNetwork);

        virtual ~IShadersManager() = default;

        virtual void CompileAppShader(const std::vector<uint8_t>& shader) = 0; // throws
        virtual void Start(const std::string& args, unsigned method, IDone& doneHandler) = 0; // throws
        virtual bool IsDone() const = 0;
    };
}
