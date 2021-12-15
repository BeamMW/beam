// Copyright 2020 The Beam Team
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
#include "ipfs_imp.h"
#include "utility/logger.h"
#include <boost/filesystem.hpp>

// TODO:IPFS checlk
// 2021/11/16 20:13:48 failed to sufficiently increase receive buffer size (was: 208 kiB, wanted: 2048 kiB, got: 416 kiB).
// See https://github.com/lucas-clemente/quic-go/wiki/UDP-Receive-Buffer-Size for details.
namespace beam::wallet
{
    IPFSService::Ptr IPFSService::create(HandlerPtr handler)
    {
        return std::make_shared<imp::IPFSService>(std::move(handler));
    }
}

namespace beam::wallet::imp
{
    namespace asio = boost::asio;

    IPFSService::IPFSService(HandlerPtr handler)
        : _handler(std::move(handler))
    {
    }

    IPFSService::~IPFSService()
    {
        assert(!_node);
        assert(!_thread);
        assert(!_ios_guard);
    }

    bool IPFSService::running() const
    {
        return _thread && _ios;
    }

    void IPFSService::start(const std::string& repoPath, asio_ipfs::config config)
    {
        if (_thread || _ios)
        {
            assert(false);
            throw std::runtime_error("IPFS Service is already running");
        }

        const auto fullPath = boost::filesystem::system_complete(repoPath);

        //
        // boost::filesystem::weakly_canonical(fullPath) doesn't throw exception
        // if something goes wrong but SIGABORT, check error and throw manually
        //
        boost::system::error_code ec;
        const auto canonicalPath = boost::filesystem::weakly_canonical(fullPath, ec);
        if (ec.failed())
        {
            throw boost::filesystem::filesystem_error("IPFS service failed to form canonical path", ec);
        }

        auto storagePath = canonicalPath.string();
        LOG_INFO() << "Starting IPFS Service. Storage path is " << storagePath;

        //
        // Startup sequence, we run it sync, in main thread. May be need to make async
        // in next versions, but for now it greatly simplifies the flow
        //
        {
            std::string error;
            auto startctx = std::make_unique<asio::io_context>();

            asio::spawn(*startctx, [&](boost::asio::yield_context yield) {
                try
                {
                    if (config.bootstrap.empty())
                    {
                        #if defined(BEAM_TESTNET)
                            #error ("Define Testnet IPFS bootstrap")
                        #elif defined(BEAM_MAINNET)
                            #error ("Define Mainnet IPFS bootstrap")
                        #else
                            // TODO:IPFS ensure boostrap is changed after startup
                            config.bootstrap.emplace_back("/ip4/3.19.141.112/tcp/38041/p2p/12D3KooWFrigFK9gVvCr7YDNNAAxDxmeyLDtR1tYvHcaXxuCcKpt");
                        #endif
                    }

                    _node = asio_ipfs::node::build(*startctx, storagePath, config, std::move(yield));

                    // TODO:IPFS ensure if connected
                    // TODO:IPFS lower connect timeout
                    assert(_node);
                }
                catch (const boost::system::system_error &err)
                {
                    error = err2str(err);
                }
            });
            startctx->run();

            if (!error.empty())
            {
                assert(false);
                throw std::runtime_error(error);
            }

            if (!_node)
            {
                assert(false);
                throw std::runtime_error("_node is somewhy empty");
            }

            // save context, since it has been running need
            // also to reset it
            _ios = std::move(startctx);
            _ios->reset();
        }

        //
        // Here we know that everything is OK, and we're ready for a real
        // threaded startup. Save data & spawn an infinitely running thread
        //
        _path = storagePath;
        _myid = _node->id();
        _ios_guard = std::make_unique<IOSGuard>(_ios->get_executor());
        _thread = std::make_unique<MyThread>([this, repo = _path]()
        {
            _ios->run();
        });
    }

    void IPFSService::stop()
    {
        if (!_node || !_thread || !_thread->joinable())
        {
            assert(false);
            throw std::runtime_error("IPFS service thread already stopped");
        }

        LOG_INFO() << "Stopping IPFS Service...";
        _node.reset();
        _ios_guard->reset();
        _ios_guard.reset();

        assert(_thread->joinable());
        _thread->join();
        _thread.reset();
        _ios.reset();
        LOG_INFO() << "IPFS Services stopped.";
    }

    void IPFSService::add(std::vector<uint8_t>&& data, std::function<void (std::string&&)>&& res, Err&& err)
    {
        if (data.empty())
        {
            retErr(std::move(err), "Empty data buffer cannot be added to IPFS");
            return;
        }

        call_ipfs(0, std::move(res), std::move(err),[this, data = std::move(data)]
        (boost::asio::yield_context yield, std::function<void()>& cancel) -> auto
        {
            return _node->add(&data[0], data.size(), cancel, std::move(yield));
        });
    }

    void IPFSService::get(const std::string& hash, uint32_t timeout, std::function<void (std::vector<uint8_t>&&)>&& res, Err&& err)
    {
        if (hash.empty())
        {
            retErr(std::move(err), "Cannot get data via an empty hash");
            return;
        }

        call_ipfs(timeout, std::move(res), std::move(err), [this, hash]
        (boost::asio::yield_context yield, std::function<void()>& cancel) -> auto
        {
            return _node->cat(hash, cancel, std::move(yield));
        });
    }

    void IPFSService::pin(const std::string& hash, uint32_t timeout, std::function<void ()>&& res, Err&& err)
    {
        if (hash.empty())
        {
            retErr(std::move(err), "Cannot get data via an empty hash");
            return;
        }

        call_ipfs(timeout, std::move(res), std::move(err), [this, hash]
        (boost::asio::yield_context yield, std::function<void()>& cancel) -> JustVoid
        {
            _node->pin(hash, cancel, std::move(yield));
            return JustVoid{};
        });
    }

    void IPFSService::unpin(const std::string& hash, uint32_t timeout, std::function<void ()>&& res, Err&& err)
    {
        if (hash.empty())
        {
            retErr(std::move(err), "Cannot unpin an empty hash");
            return;
        }

        call_ipfs(timeout, std::move(res), std::move(err), [this, hash]
        (boost::asio::yield_context yield, std::function<void()>& cancel) -> JustVoid
        {
            _node->unpin(hash, cancel, std::move(yield));
            return JustVoid{};
        });
    }

    void IPFSService::gc(uint32_t timeout, std::function<void ()>&& res, Err&& err)
    {
        call_ipfs(timeout, std::move(res), std::move(err), [this]
        (boost::asio::yield_context yield, std::function<void()>& cancel) -> JustVoid
        {
            _node->gc(cancel, std::move(yield));
            return JustVoid{};
        });
    }

    void IPFSService::retToClient(std::function<void()>&& what)
    {
        _handler->pushToClient(std::move(what));
    }
}
