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
#include "external_pow.h"
#include "stratum.h"
#include "p2p/line_protocol.h"
#include "utility/io/tcpserver.h"
#include "utility/io/coarsetimer.h"
#include <set>
#include <map>

namespace beam { namespace stratum {

struct ConnectionToServer {
    virtual ~ConnectionToServer() = default;

    virtual bool on_login(uint64_t from, const Login& login) = 0;
    virtual bool on_solution(uint64_t from, const Solution& solution) = 0;
    virtual void on_bad_peer(uint64_t from) = 0;
};

class Server : public IExternalPOW, public ConnectionToServer {
public:
    Server(const IExternalPOW::Options& o, io::Reactor& reactor, io::Address listenTo);

private:
    class AccessControl {
    public:
        explicit AccessControl(const std::string& keysFileName);

        bool check(const std::string& key);

        void refresh();
    private:
        bool _enabled;
        std::string _keysFileName;
        time_t _lastModified;
        std::set<std::string> _keys;
    };

    class Connection : public ParserCallback {
    public:
        Connection(ConnectionToServer& owner, uint64_t id, io::TcpStream::Ptr&& newStream);

        void set_logged_in() { _loggedIn = true; }

        bool send_msg(const io::SerializedMsg& msg, bool onlyIfLoggedIn, bool shutdown=false);

    private:
        bool on_message(const Login& login) override;

        bool on_message(const Solution& solution) override;

        bool on_raw_message(void* data, size_t size);

        bool on_stratum_error(ResultCode code) override;

        bool on_unsupported_stratum_method(Method method) override;

        bool on_stream_data(io::ErrorCode errorCode, void* data, size_t size);

        ConnectionToServer& _owner;
        uint64_t _id;
        io::TcpStream::Ptr _stream;
        LineReader _lineReader;
        bool _loggedIn;
    };

    struct JobCtx {
        io::SerializedMsg msg;
        std::string id;
        Block::PoW pow;
        BlockFound onBlockFound;
        CancelCallback cancelFn;
    };

    void start_server();

    void refresh_acl();

    void on_stream_accepted(io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode);

    bool on_login(uint64_t from, const Login& login) override;
    bool on_solution(uint64_t from, const Solution& solution) override;
    void on_bad_peer(uint64_t from) override;

    void new_job(
        const std::string&,
        const Merkle::Hash& input, const Block::PoW& pow,
        const BlockFound& callback,
        const CancelCallback& cancelCallback
    ) override;

    void get_last_found_block(std::string& jobID, Block::PoW& pow) override;
    void stop_current() override;
    void stop() override;

    Options _options;
    io::Reactor& _reactor;
    io::Address _bindAddress;
    io::MultipleTimers _timers;
    io::FragmentWriter _fw;
    io::TcpServer::Ptr _server;
    std::map<uint64_t, std::unique_ptr<Connection>> _connections;
    AccessControl _acl;
    JobCtx _job;
    io::SerializedMsg _currentMsg;
    std::vector<uint64_t> _deadConnections;
};

}} //namespaces
