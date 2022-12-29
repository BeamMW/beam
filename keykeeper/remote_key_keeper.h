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

#include "../wallet/core/private_key_keeper.h"
#include <mutex>
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/set.hpp>

namespace beam::wallet
{
    class RemoteKeyKeeper
        : public PrivateKeyKeeper_WithMarshaller
    {
        struct Impl;

    public:
        RemoteKeyKeeper();
        virtual ~RemoteKeyKeeper() = default;

        // special cases, attempt to get result from cache, bypassing the async mechanism
        Status::Type InvokeSync(Method::get_Kdf& m) override;
        Status::Type InvokeSync(Method::get_NumSlots& m) override;

#define THE_MACRO(method) \
        void InvokeAsync(Method::method& m, const Handler::Ptr& pHandler) override;

        KEY_KEEPER_METHODS(THE_MACRO)
#undef THE_MACRO

    protected:

        // communication with the remote
        virtual void SendRequestAsync(void* pBuf, uint32_t nRequest, uint32_t nResponse, const Handler::Ptr& pHandler) = 0;

        static Status::Type DeduceStatus(uint8_t* pBuf, uint32_t nResponse, uint32_t nResponseActual);

        struct Cache
        {
            std::mutex m_Mutex;

            Key::IPKdf::Ptr m_pOwner;

            uint32_t m_Slots = 0;
            uint32_t get_NumSlots();

            bool get_Owner(Key::IPKdf::Ptr&);
            void set_Owner(const Key::IPKdf::Ptr&);
        };

        Cache m_Cache;
    };
}
