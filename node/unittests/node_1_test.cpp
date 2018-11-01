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

#include "../node.h"
#include "../../core/ecc_native.h"

#ifndef LOG_VERBOSE_ENABLED
    #define LOG_VERBOSE_ENABLED 0
#endif
#include "utility/logger.h"
#include "utility/test_helpers.h"

int g_Ret = 0;

namespace ECC {

	void GenerateRandom(void* p, uint32_t n)
	{
		for (uint32_t i = 0; i < n; i++)
			((uint8_t*) p)[i] = (uint8_t) rand();
	}

	void SetRandom(uintBig& x)
	{
		GenerateRandom(x.m_pData, x.nBytes);
	}

	void SetRandom(Scalar::Native& x)
	{
		Scalar s;
		while (true)
		{
			SetRandom(s.m_Value);
			if (!x.Import(s))
				break;
		}
	}
}

namespace beam
{
#ifdef WIN32
		const char* g_sz = "mytest.db";
#else // WIN32
		const char* g_sz = "/tmp/mytest.db";
#endif // WIN32

	void TestNode1(unsigned nReconnects, unsigned timerInterval)
	{
		io::Reactor::Ptr pReactor(io::Reactor::create());
        io::Reactor::Scope scope(*pReactor);

		Node node;
		node.m_Cfg.m_sPathLocal = g_sz;
		node.m_Cfg.m_Listen.port(Node::s_PortDefault);
		node.m_Cfg.m_Listen.ip(INADDR_ANY);

		node.Initialize();

		struct MyClient
			:public proto::NodeConnection
		{
			const unsigned mTimerInterval;
            unsigned mReconnects;

            bool m_bConnected;

			MyClient(unsigned interval, unsigned reconnects):
                mTimerInterval(interval), mReconnects(reconnects)
			{
				m_pTimer = io::Timer::create(io::Reactor::get_Current());
				m_bConnected = false;
			}

			virtual void OnConnectedSecure() override {

				m_bConnected = true;

				try {
					proto::GetHdr msg;
					msg.m_ID.m_Height = 0;
					ZeroObject(msg.m_ID.m_Hash);

					Send(msg);

					SetTimer(mTimerInterval); // for reconnection

				} catch (...) {
					OnFail();
				}
			}

			virtual void OnDisconnect(const DisconnectReason&) override {
				OnFail();
			}

			void OnFail() {
				Reset();
				SetTimer(mTimerInterval);
				m_bConnected = false;
			}

			io::Timer::Ptr m_pTimer;
			void OnTimer() {

				if (!m_bConnected)
				{
					Reset();

                    if (mReconnects-- == 0) {
                        io::Reactor::get_Current().stop();
                    }

					try {

						io::Address addr;
						addr.resolve("127.0.0.1");
						addr.port(Node::s_PortDefault);

						Connect(addr);

					}
					catch (...) {
						OnFail();
					}
				}
				else
					OnFail();
			}

			void SetTimer(uint32_t timeout_ms) {
				m_pTimer->start(timeout_ms, false, [this]() { return (this->OnTimer)(); });
			}
			void KillTimer() {
				m_pTimer->cancel();
			}
		};

		MyClient cl(timerInterval, nReconnects);

		cl.SetTimer(timerInterval);

		pReactor->run();
        LOG_VERBOSE() << pReactor.use_count();
	}

    void TestP2pSane()
	{
		io::Reactor::Ptr pReactor(io::Reactor::create());
		io::Reactor::Scope scope(*pReactor);

		struct MyConnection
			:public proto::NodeConnection
		{
			uint32_t m_In = 0;
			uint32_t m_Out = 0;
			uint32_t m_Batch = 0;

			io::Timer::Ptr m_pTimer;

			void OnTimer()
			{
				proto::GetHdr msg;
				ZeroObject(msg.m_ID.m_Hash);

				for (int i = 0; i < 15; i++)
				{
					msg.m_ID.m_Height = m_Out++;
                    LOG_VERBOSE() << TRACE(msg.m_ID.m_Height);
					Send(msg);
				}

				if (++m_Batch < 300)
					SetTimer(10);
				else
					io::Reactor::get_Current().stop();
			}

			void SetTimer(uint32_t timeout_ms) {
				m_pTimer->start(timeout_ms, false, [this]() { return (this->OnTimer)(); });
			}

			virtual void OnConnectedSecure() override
			{
				m_pTimer = io::Timer::create(io::Reactor::get_Current());
				OnTimer();
			}

			virtual void OnDisconnect(const DisconnectReason&) override
			{
				printf("OnDisconnect\n");
				g_Ret = 1;
				io::Reactor::get_Current().stop();
			}

			virtual void OnMsg(proto::GetHdr&& msg) override
			{
                LOG_VERBOSE() << TRACE(m_In) << TRACE(msg.m_ID.m_Height);
				if (msg.m_ID.m_Height != m_In)
				{
                    printf("OnMsg gap: %u - %u\n", m_In, (uint32_t) msg.m_ID.m_Height);
					g_Ret = 1;
				}

				m_In = (uint32_t)msg.m_ID.m_Height + 1;
			}
		};

		struct Srv :public proto::NodeConnection::Server
		{
			MyConnection m_Conn2;

			virtual void OnAccepted(io::TcpStream::Ptr&& pStream, int errorCode)
			{
				m_pServer = NULL; // no more accepts

				if (!pStream)
				{
					printf("accept failed\n");
					g_Ret = 1;
					io::Reactor::get_Current().stop();
				}

				m_Conn2.Accept(std::move(pStream));
				m_Conn2.SecureConnect();
			}
		};

		io::Address addr;
		addr.resolve("127.0.0.1:29065");

		Srv srv;
		srv.Listen(addr);

		MyConnection conn;
		conn.Connect(addr);

		pReactor->run();
	}


}

int main()
{
    int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
    logLevel = LOG_LEVEL_VERBOSE;
#endif
    auto logger = beam::Logger::create(logLevel, logLevel);

	beam::helpers::ProcessWideLock("/tmp/BEAM_node_test_lock");

    beam::TestP2pSane();
    beam::TestNode1(10, 100);

    return g_Ret;
}
