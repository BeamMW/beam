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
#include "errorhandling.h"
#include "mempool.h"
#include "address.h"
#include "bufferchain.h"
#include <memory>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace beam { namespace io {

class TcpStream;
class CoarseTimer;
class TcpConnectors;
class TcpShutdowns;
class PendingWrites;
class SslStream;

class Reactor : public std::enable_shared_from_this<Reactor> {
public:
    Reactor(const Reactor&) = delete;
    Reactor& operator=(const Reactor&) = delete;

    using Ptr = std::shared_ptr<Reactor>;

    /// Creates a new reactor. Throws on errors
    static Ptr create();

    /// Performs shutdown and cleanup.
    virtual ~Reactor();

    /// Runs the reactor. This function blocks.
    // TODO: Consider if it is possible to move callback to constructor, may be would be possible after task #739
    using StopCallback = std::function<void()>;
    void run_ex(StopCallback&& scb);
    void run();

    /// Stops the running reactor.
    /// NOTE: Called from another thread.
    void stop();

    using ConnectCallback = std::function<void(uint64_t tag, std::unique_ptr<TcpStream>&& newStream, ErrorCode errorCode)>;
    Result tcp_connect(
        Address address,
        uint64_t tag,
        const ConnectCallback& callback,
        int timeoutMsec=-1,
        bool tlsConnect=false,
        Address bindTo=Address()
    );

    void cancel_tcp_connect(uint64_t tag);

	class Scope
	{
		Reactor* m_pPrev;
	public:
		Scope(Reactor&);
		~Scope();
	};

	static Reactor& get_Current();
	uv_loop_t& get_UvLoop() { return _loop; }

	class GracefulIntHandler
	{
		static Reactor* s_pAppReactor;

#ifdef WIN32
		static BOOL WINAPI Handler(DWORD dwCtrlType);
#else // WIN32
		void SetHandler(bool);
		static void Handler(int sig);
#endif // WIN32

	public:
		GracefulIntHandler(Reactor&);
		~GracefulIntHandler();
	};

private:
    /// Ctor. private and called by create()
    Reactor();

    /// Pollable objects' base
    struct Object {
        Object() = default;
        Object(const Object&) = delete;
        Object& operator=(const Object&) = delete;

        Object(Object&& o) :
            _reactor(std::move(o._reactor)),
            _handle(o._handle)
        {
            o._handle = 0;
        }

        Object& operator=(Object&& o) {
            _reactor = std::move(o._reactor);
            _handle = o._handle;
            o._handle = 0;
            return *this;
        }

        virtual ~Object() {
            async_close();
        }

        void async_close() {
            if (_handle) {
                _handle->data = 0;
                if (_reactor) {
                    _reactor->async_close(_handle);
                    _reactor.reset();
                }
                else if (_handle->loop->data) {
                    // object owned by Reactor itself
                    reinterpret_cast<Reactor*>(_handle->loop->data)->async_close(_handle);
                }
            }
        }

        Reactor::Ptr _reactor;
        uv_handle_t* _handle = nullptr;
    };

    ErrorCode init_asyncevent(Object* o, uv_async_cb cb);

    ErrorCode init_timer(Object* o);
    ErrorCode start_timer(Object* o, unsigned intervalMsec, bool isPeriodic, uv_timer_cb cb);
    void cancel_timer(Object* o);

    ErrorCode init_tcpserver(Object* o, Address bindAddress, uv_connection_cb cb);
    ErrorCode init_tcpstream(Object* o);
    ErrorCode accept_tcpstream(Object* acceptor, Object* newConnection);
    TcpStream* stream_connected(TcpStream* stream, uv_handle_t* h);
    void shutdown_tcpstream(Object* o);

    using OnDataWritten = std::function<void(ErrorCode, size_t)>;
    ErrorCode async_write(Reactor::Object* o, BufferChain& unsent, const OnDataWritten& cb);

    ErrorCode init_object(ErrorCode errorCode, Object* o, uv_handle_t* h);
    void async_close(uv_handle_t*& handle);

    union Handles {
        uv_timer_t timer;
        uv_async_t async;
        uv_tcp_t tcp;
    };

    uv_loop_t _loop;
    uv_async_t _stopEvent;
    MemPool<uv_handle_t, sizeof(Handles)> _handlePool;
    bool _creatingInternalObjects=false;

    std::unique_ptr<PendingWrites> _pendingWrites;
    std::unique_ptr<TcpConnectors> _tcpConnectors;
    std::unique_ptr<TcpShutdowns>  _tcpShutdowns;
    StopCallback _stopCB;

    friend class TcpConnectors;
    friend class TcpShutdowns;
    friend class PendingWrites;
    friend class AsyncEvent;
    friend class Timer;
    friend class TcpServer;
    friend class SslServer;
    friend class TcpStream;
};

}} //namespaces
