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

#include "external_pow.h"
#include "stratum.h"
#include "p2p/line_protocol.h"
#include "utility/io/tcpstream.h"
#include "utility/io/timer.h"
#include "utility/helpers.h"
#include <boost/program_options.hpp>

#define LOG_VERBOSE_ENABLED 1
#include "utility/logger.h"

namespace po = boost::program_options;

namespace beam {

static const unsigned RECONNECT_TIMEOUT = 1000;

class StratumClient : public stratum::ParserCallback {
    std::unique_ptr<IExternalPOW> _miner;
    io::Reactor& _reactor;
    io::Address _serverAddress;
    std::string _apiKey;
    LineProtocol _lineProtocol;
    io::TcpStream::Ptr _connection;
    io::Timer::Ptr _timer;
    std::string _lastJobID;
    Merkle::Hash _lastJobInput;
    Block::PoW _lastFoundBlock;
    bool _blockSent;
    bool _tls;

public:
    StratumClient(io::Reactor& reactor, const io::Address& serverAddress, std::string apiKey, bool no_tls) :
        _reactor(reactor),
        _serverAddress(serverAddress),
        _apiKey(std::move(apiKey)),
        _lineProtocol(
            BIND_THIS_MEMFN(on_raw_message),
            BIND_THIS_MEMFN(on_write)
        ),
        _timer(io::Timer::create(_reactor)),
        _blockSent(false),
        _tls(!no_tls)
    {
        _timer->start(0, false, BIND_THIS_MEMFN(on_reconnect));
        _miner = IExternalPOW::create_local_solver();
    }

private:
    bool on_raw_message(void* data, size_t size) {
        LOG_DEBUG() << "got " << std::string((char*)data, size);
        return stratum::parse_json_msg(data, size, *this);
    }

    bool fill_job_info(const stratum::Job& job) {
        bool ok = false;
        std::vector<uint8_t> buf = from_hex(job.input, &ok);
        if (!ok || buf.size() != 32) return false;
        memcpy(_lastJobInput.m_pData, buf.data(), 32);
        _lastJobID = job.id;
        return true;
    }

    bool on_message(const stratum::Job& job) override {
        LOG_INFO() << "new job here..." << TRACE(job.input) << TRACE(job.difficulty);

        Block::PoW pow;
        pow.m_Difficulty.m_Packed = job.difficulty;

        if (!fill_job_info(job)) return false;

        _miner->new_job(
            _lastJobID, _lastJobInput, pow,
            BIND_THIS_MEMFN(on_block_found),
            []() { return false; }
        );

        return true;
    }

    void on_block_found() {
        std::string jobID;
        _miner->get_last_found_block(jobID, _lastFoundBlock);
        if (jobID != _lastJobID) {
            LOG_INFO() << "solution expired" << TRACE(jobID);
            return;
        }

        char buf[72];
        LOG_DEBUG() << "input=" << to_hex(buf, _lastJobInput.m_pData, 32);

        if (!_lastFoundBlock.IsValid(_lastJobInput.m_pData, 32)) {
            LOG_ERROR() << "solution is invalid";
            return;
        }
        LOG_INFO() << "block found id=" << _lastJobID;



        _blockSent = false;
        send_last_found_block();
    }

    void send_last_found_block() {
        if (_blockSent || !_connection || !_connection->is_connected()) return;
        stratum::Solution sol(_lastJobID, _lastFoundBlock);
        if (!stratum::append_json_msg(_lineProtocol, sol)) {
            LOG_ERROR() << "Internal error";
            _reactor.stop();
            return;
        }
        _lineProtocol.finalize();
    }

    bool on_stratum_error(stratum::ResultCode code) override {
        if (code == stratum::login_failed) {
            LOG_ERROR() << "Login to " << _serverAddress << " failed, try again later";
            _reactor.stop();
            return false;
        }

        // TODO what to do with other errors
        LOG_ERROR() << "got stratum error: " << code << " " << stratum::get_result_msg(code);
        return true;
    }

    bool on_unsupported_stratum_method(stratum::Method method) override {
        LOG_INFO() << "ignoring unsupported stratum method: " << stratum::get_method_str(method);
        return true;
    }

    void on_write(io::SharedBuffer&& msg) {
        if (_connection) {
            LOG_VERBOSE() << "writing " << std::string((const char*)msg.data, msg.size - 1);
            auto result = _connection->write(msg);
            if (!result) {
                on_disconnected(result.error());
            } else {
                _blockSent = true; //TODO ???
            }
        } else {
            LOG_DEBUG() << "ignoring message, no connection";
        }
    }

    void on_disconnected(io::ErrorCode error) {
        LOG_INFO() << "disconnected, error=" << io::error_str(error) << ", rescheduling";
        _connection.reset();
        _timer->start(RECONNECT_TIMEOUT, false, BIND_THIS_MEMFN(on_reconnect));
    }

    void on_reconnect() {
        LOG_INFO() << "connecting to " << _serverAddress;
        if (!_reactor.tcp_connect(_serverAddress, 1, BIND_THIS_MEMFN(on_connected), 10000, _tls)) {
            LOG_ERROR() << "connect attempt failed, rescheduling";
            _timer->start(RECONNECT_TIMEOUT, false, BIND_THIS_MEMFN(on_reconnect));
        }
    }

    void on_connected(uint64_t, io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode) {
        if (errorCode != 0) {
            on_disconnected(errorCode);
            return;
        }

        LOG_INFO() << "connected to " << _serverAddress;
        _connection = std::move(newStream);
        _connection->enable_keepalive(2);
        _connection->enable_read(BIND_THIS_MEMFN(on_stream_data));

        if (!stratum::append_json_msg(_lineProtocol, stratum::Login(_apiKey))) {
            LOG_ERROR() << "Internal error";
            _reactor.stop();
        }
        if (!_blockSent) {
            _lineProtocol.finalize();
        } else {
            send_last_found_block();
        }
    }

    bool on_stream_data(io::ErrorCode errorCode, void* data, size_t size) {
        if (errorCode != 0) {
            on_disconnected(errorCode);
            return false;
        }
        if (!_lineProtocol.new_data_from_stream(data, size)) {
            //TODO stream corrupted
        }
        return true;
    }
};

} //namespace

struct Options {
    std::string apiKey;
    std::string serverAddress;
    bool no_tls=false;
    int logLevel=LOG_LEVEL_DEBUG;
    static const unsigned logRotationPeriod = 3*60*60*1000; // 3 hours
};

static bool parse_cmdline(int argc, char* argv[], Options& o);

int main(int argc, char* argv[]) {
    using namespace beam;

    Options options;
    if (!parse_cmdline(argc, argv, options)) {
        return 1;
    }
    auto logger = Logger::create(LOG_LEVEL_INFO, options.logLevel, options.logLevel, "miner_client_");
    int retCode = 0;
    try {
        io::Address connectTo;
        if (!connectTo.resolve(options.serverAddress.c_str())) {
            throw std::runtime_error(std::string("cannot resolve server address ") + options.serverAddress);
        }
        io::Reactor::Ptr reactor = io::Reactor::create();
        io::Reactor::Scope scope(*reactor);
        io::Reactor::GracefulIntHandler gih(*reactor);
        io::Timer::Ptr logRotateTimer = io::Timer::create(*reactor);
        logRotateTimer->start(
            options.logRotationPeriod, true, []() { Logger::get()->rotate(); }
        );
        StratumClient client(*reactor, connectTo, options.apiKey, options.no_tls);
        reactor->run();
        LOG_INFO() << "Done";
    } catch (const std::exception& e) {
        LOG_ERROR() << "EXCEPTION: " << e.what();
        retCode = 255;
    } catch (...) {
        LOG_ERROR() << "NON_STD EXCEPTION";
        retCode = 255;
    }
    return retCode;
}

bool parse_cmdline(int argc, char* argv[], Options& o) {

    po::options_description cliOptions("Remote miner options");
    cliOptions.add_options()
    ("help", "list of all options")
    ("server", po::value<std::string>(&o.serverAddress)->required(), "server address")
    ("key", po::value<std::string>(&o.apiKey)->required(), "api key")
    ("no-tls", po::bool_switch(&o.no_tls)->default_value(false), "disable tls")
    ;

#ifdef NDEBUG
    o.logLevel = LOG_LEVEL_DEBUG;
#else
#if LOG_VERBOSE_ENABLED
    o.logLevel = LOG_LEVEL_VERBOSE;
#else
    o.logLevel = LOG_LEVEL_DEBUG;
#endif
#endif

    po::variables_map vm;
    try
    {
        po::store(po::command_line_parser(argc, argv) // value stored first is preferred
                  .options(cliOptions)
                  .run(), vm);

        if (vm.count("help")) {
            std::cout << cliOptions << std::endl;
            return false;
        }

        vm.notify();

        return true;
    } catch (const po::error& ex) {
        std::cerr << ex.what() << "\n" << cliOptions;
    } catch (const std::exception& ex) {
        std::cerr << ex.what();
    } catch (...) {
        std::cerr << "NON_STD EXCEPTION";
    }

    return false;
}