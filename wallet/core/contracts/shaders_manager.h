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

#include "utility/logger.h"
#include "i_shaders_manager.h"

namespace beam::wallet {

    class ShadersManager
        : public IShadersManager
        , private beam::bvm2::ManagerStd
    {
    public:
        ShadersManager(beam::wallet::Wallet::Ptr wallet,
                       beam::wallet::IWalletDB::Ptr walletDB,
                       beam::proto::FlyClient::INetwork::Ptr nodeNetwork);

        bool IsDone() const override
        {
            return _done;
        }

        void CompileAppShader(const std::vector<uint8_t> &shader) override;// throws
        void Start(const std::string &args, unsigned method, IDone& doneHandler); // throws

    protected:
        void OnDone(const std::exception *pExc) override;

    private:
        bool _done = true;
        bool _async = false;

        beam::wallet::IWalletDB::Ptr _wdb;
        beam::wallet::Wallet::Ptr _wallet;
        IDone* _doneHandler = nullptr;
    };
}
