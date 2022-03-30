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

namespace Env {
#include "../bvm2_cost.h"
} // namespace Env

struct Comm
{
    class Channel
    {
        Env::KeyID m_Kid;
        const PubKey* m_pRemote = nullptr;
        bool m_Waiting = false;

        uint32_t get_Cookie() const
        {
#if defined(HOST_BUILD) || defined(_MSC_VER)
            return (uint32_t)(size_t) this;
#else // HOST_BUILD
            return reinterpret_cast<uint32_t>(this);
#endif // HOST_BUILD
        }

        void ReadMsg(uint32_t nSize)
        {
            if (m_Waiting && (nSize >= sizeof(HashValue)))
            {
                m_vMsg.Prepare(nSize);
                Env::Comm_Read(m_vMsg.m_p, nSize, nullptr, 0);

                HashProcessor::Base hp0;
                hp0.m_p = Env::HashClone(m_Context.m_p);

                uint32_t nSizeNetto = nSize - sizeof(HashValue);
                m_Context.Write(m_vMsg.m_p, nSizeNetto);

                HashValue hvExp;
                m_Context >> hvExp;

                if (_POD_(hvExp) == *(HashValue*) (m_vMsg.m_p + nSizeNetto))
                {
                    m_vMsg.m_Count = nSizeNetto;
                    m_Waiting = false;
                }
                else
                    std::swap(hp0.m_p, m_Context.m_p); // restore context
            }
            else
                // just skip it
                Env::Comm_Read(nullptr, 0, nullptr, 0);
        }

    public:

        HashProcessor::Base m_Context;

        Utils::Vector<uint8_t> m_vMsg; // last received msg

        bool IsInitialized() const {
            return !!m_Context.m_p;
        }

        void Init(const Env::KeyID& kid, const PubKey& pkRemote, const Secp::Oracle* pContext = nullptr)
        {
            assert(!IsInitialized());
            m_Kid = kid;
            m_pRemote = &pkRemote;

            if (pContext)
                m_Context.m_p = Env::HashClone(pContext->m_p);
            else
            {
                HashProcessor::Sha256 hp;
                std::swap(m_Context.m_p, hp.m_p);
            }

            Secp::Point p0;
            p0.Import(pkRemote); // dont care if failed
            Env::get_PkEx(p0, p0, kid.m_pID, kid.m_nID);

            PubKey pkShared;
            p0.Export(pkShared);

            m_Context << pkShared;

            m_Kid.Comm_Listen(get_Cookie());
        }

        Channel() {}

        Channel(const Env::KeyID& kid, const PubKey& pkRemote, const Secp::Oracle* pContext = nullptr)
        {
            Init(kid, pkRemote, pContext);
        }

        ~Channel()
        {
            if (IsInitialized())
                Env::Comm_Listen(nullptr, 0, get_Cookie());
        }

        void Send(const void* pMsg, uint32_t nMsg)
        {
            m_Context.Write(pMsg, nMsg);

            uint32_t nSizePlus = sizeof(HashValue) + nMsg;
            auto* pBuf = (uint8_t*) Env::StackAlloc(nSizePlus);

            Env::Memcpy(pBuf, pMsg, nMsg);
            m_Context >> *(HashValue*) (pBuf + nMsg);

            Env::Comm_Send(*m_pRemote, pBuf, nSizePlus);
        }

        template <typename T>
        void Send_T(const T& x)
        {
            Send(&x, sizeof(x));
        }

        void RcvStart()
        {
            m_Waiting = true;
        }

        void RcvWait()
        {
            while (m_Waiting)
            {
                uint32_t nCookie;
                uint32_t nSize = Env::Comm_Read(nullptr, 0, &nCookie, 1);
                if (nSize)
#if defined(HOST_BUILD) || defined(_MSC_VER)
                    ((Channel*)(size_t) nCookie)->ReadMsg(nSize);
#else // HOST_BUILD
                    ((Channel*) nCookie)->ReadMsg(nSize);
#endif // HOST_BUILD
                else
                    Env::Comm_WaitMsg();
            }
        }

        void RcvWaitExact(uint32_t nSize, bool bStartNow = true)
        {
            if (bStartNow)
                RcvStart();

            while (true)
            {
                RcvWait();
                if (m_vMsg.m_Count == nSize)
                    break;
                RcvStart();
            }
        }

        template <typename T>
        T& Rcv_T(bool bStartNow = true)
        {
            RcvWaitExact(sizeof(T), bStartNow);
            return *(T*) m_vMsg.m_p;
        }

        template <typename T>
        void Rcv_T(T& x, bool bStartNow = true)
        {
            _POD_(x) = Rcv_T<T>(bStartNow);
        }
    };
};
