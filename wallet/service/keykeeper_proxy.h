// Copyright 2018-2020 The Beam Team
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

#include <memory>
#include "keykeeper/local_private_key_keeper.h"
#include "nlohmann/json.hpp"

namespace beam::wallet {
    using json = nlohmann::json;

    struct IKeykeeperConnection
    {
        using KeyKeeperFunc = std::function<void(const json&)>;
        virtual void invokeKeykeeperAsync(const json& msg, KeyKeeperFunc func) = 0;
    };

    class WasmKeyKeeperProxy
        : public PrivateKeyKeeper_WithMarshaller
        , public std::enable_shared_from_this<WasmKeyKeeperProxy>
    {
    public:
        WasmKeyKeeperProxy(Key::IPKdf::Ptr ownerKdf, IKeykeeperConnection& connection);
        ~WasmKeyKeeperProxy() override = default;

        Status::Type InvokeSync(Method::get_Kdf& x) override;

        void InvokeAsync(Method::get_Kdf& x, const Handler::Ptr& h) override;
        void InvokeAsync(Method::get_NumSlots& x, const Handler::Ptr& h) override;
        void InvokeAsync(Method::CreateOutput& x, const Handler::Ptr& h) override;
        void InvokeAsync(Method::SignReceiver& x, const Handler::Ptr& h) override;
        void InvokeAsync(Method::SignSender& x, const Handler::Ptr& h) override;
        void InvokeAsync(Method::SignSplit& x, const Handler::Ptr& h) override;

        static void GetMutualResult(Method::TxMutual& x, const json& msg);
        static void GetCommonResult(Method::TxCommon& x, const json& msg);
        static Status::Type GetStatus(const json& msg);

    private:
        Key::IPKdf::Ptr _ownerKdf;
        IKeykeeperConnection& _connection;
    };
}