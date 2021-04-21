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

#include "bvm/invoke_data.h"
#include "wallet/core/wallet_db.h"
#include "wallet/core/wallet.h"

namespace beam::wallet
{
    class IShadersManager
    {
    public:
        typedef std::function <void (boost::optional<TxID> txid, boost::optional<std::string> output, boost::optional<std::string> error)> DoneAllHandler;
        typedef std::function <void (boost::optional<ByteBuffer> data, boost::optional<std::string> output, boost::optional<std::string> error)> DoneCallHandler;
        typedef std::function <void (boost::optional<TxID> txid, boost::optional<std::string> error)> DoneTxHandler;

        typedef std::shared_ptr<IShadersManager> Ptr;
        typedef std::weak_ptr<IShadersManager> WeakPtr;

        static Ptr CreateInstance(
                beam::wallet::Wallet::Ptr wallet,
                beam::wallet::IWalletDB::Ptr wdb,
                beam::proto::FlyClient::INetwork::Ptr nodeNetwork);

        virtual ~IShadersManager() = default;

        // Compile throws on error!
        virtual void CompileAppShader(const std::vector<uint8_t>& shader) = 0;

        // One active call only. You cannot start another function call while previous one is not done (while !IsDone())
        // CallShaderAndStartTx - call shader & automatically create transaction if necessary
        // CallShader - only make call and return tx data, doesn't create any transactions
        // ProcessTxData - process data returned by CallShader
        virtual void CallShaderAndStartTx(const std::string& args, unsigned method, DoneAllHandler doneHandler) = 0;
        virtual void CallShader(const std::string& args, unsigned method, DoneCallHandler) = 0;
        virtual void ProcessTxData(const ByteBuffer& data, DoneTxHandler doneHandler) = 0;
        [[nodiscard]] virtual  bool IsDone() const = 0;

        // ugly but will work for the moment
        virtual void SetCurrentApp(const std::string& appid, const std::string& appname) = 0; // throws
        virtual void ReleaseCurrentApp(const std::string& appid) = 0; // throws
    };
}
