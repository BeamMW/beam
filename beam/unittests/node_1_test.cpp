#include "../node.h"
#include "../../core/ecc_native.h"

#define LOG_VERBOSE_ENABLED 0
#include "utility/logger.h"

namespace ECC {

	Context g_Ctx;
	const Context& Context::get() { return g_Ctx; }

	void GenerateRandom(void* p, uint32_t n)
	{
		for (uint32_t i = 0; i < n; i++)
			((uint8_t*) p)[i] = (uint8_t) rand();
	}

	void SetRandom(uintBig& x)
	{
		GenerateRandom(x.m_pData, sizeof(x.m_pData));
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
				m_pTimer = io::Timer::create(io::Reactor::get_Current().shared_from_this());
				m_bConnected = false;
			}

			virtual void OnConnected() override {

				m_bConnected = true;

				try {
					proto::IsHasBody msg;
					msg.m_ID.m_Height = 0;
					ZeroObject(msg.m_ID.m_Hash);

					Send(msg);

					SetTimer(mTimerInterval); // for reconnection

				} catch (...) {
					OnFail();
				}
			}

			virtual void OnClosed(int errorCode) override {
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

}

int main()
{
    beam::LoggerConfig lc;
    int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
    logLevel = LOG_LEVEL_VERBOSE;
#endif
    lc.consoleLevel = logLevel;
    lc.flushLevel = logLevel;
    auto logger = beam::Logger::create(lc);
        
    beam::TestNode1(10, 100);
        
    return 0;
}
