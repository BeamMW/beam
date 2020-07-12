// Copyright 2019 The Beam Team
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

#include "wallet/core/private_key_keeper.h"
#include "hw_wallet.h"
#include <queue>
#include <functional>
#include <mutex>
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/set.hpp>

namespace beam::wallet
{
    class TrezorKeyKeeperProxy
        : public PrivateKeyKeeper_WithMarshaller
    {
        using MessageHandler = std::function<void()>;
    public:
        TrezorKeyKeeperProxy(std::shared_ptr<Client> client, const std::string& deviceName, HWWallet::IHandler::Ptr uiHandler = {});
        virtual ~TrezorKeyKeeperProxy() = default;
    private:
        Status::Type InvokeSync(Method::get_Kdf& m) override;
        Status::Type InvokeSync(Method::get_NumSlots& m) override;

#define THE_MACRO(method) \
        void InvokeAsync(Method::method& m, const Handler::Ptr& pHandler) override;

        KEY_KEEPER_METHODS(THE_MACRO)
#undef THE_MACRO

    private:
        std::shared_ptr<Client> m_Client;
        std::shared_ptr<DeviceManager> m_DeviceManager;
        std::string m_DeviceName;

        struct Cache
        {
            std::mutex m_Mutex;

            Key::IPKdf::Ptr m_pOwner;

            struct ChildPKdf
		        :public boost::intrusive::set_base_hook<>
		        ,public boost::intrusive::list_base_hook<>
            {
                Key::Index m_iChild;
                Key::IPKdf::Ptr m_pRes;
                bool operator < (const ChildPKdf& v) const { return (m_iChild < v.m_iChild); }
            };

            typedef boost::intrusive::list<ChildPKdf> ChildPKdfList;
            typedef boost::intrusive::multiset<ChildPKdf> ChildPKdfSet;

            ChildPKdfList m_mruPKdfs;
            ChildPKdfSet m_setPKdfs;

            uint32_t m_Slots = 0;

            ~Cache() { ShrinkMru(0); }

            void ShrinkMru(uint32_t);

            bool FindPKdf(Key::IPKdf::Ptr&, const Key::Index* pChild);
            void AddPKdf(const Key::IPKdf::Ptr&, const Key::Index* pChild);

        private:
            void AddMru(ChildPKdf&);
        };

        Cache m_Cache;

        struct CreateOutputCtx;
        void PushOut1(Task::Ptr& p);

        void PushHandler(const Handler::Ptr& handler);
        void PopHandler();
        void ShowUI();
        void HideUI();
        std::shared_ptr<DeviceManager> GetDevice();

        std::queue<Handler::Ptr> m_Handlers;
        HWWallet::IHandler::Ptr m_UIHandler;
    };
}