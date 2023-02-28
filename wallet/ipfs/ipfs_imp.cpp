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

namespace beam::wallet
{
    IPFSService::Ptr IPFSService::AnyThread_create(HandlerPtr handler)
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
        assert(!_thread.joinable());
        assert(!_ios_guard);

        if (_thread.joinable()) {
            std::terminate();
        }
    }

    void IPFSService::ServiceThread_start(asio_ipfs::config config)
    {
        std::scoped_lock lock(_mutex);
        if (_thread.joinable())
        {
            throw std::runtime_error("IPFS Service is already running");
        }

        const auto fullPath = boost::filesystem::system_complete(config.repo_root);

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

        config.repo_root = canonicalPath.string();
        LOG_INFO() << "Starting IPFS Service. Repo path is " << config.repo_root;
        asio_ipfs::node::redirect_logs([] (const char* what) {
           LOG_INFO() << what;
        });

        //
        // Startup sequence, we run it sync, in main thread. May be need to make async
        // in next versions, but for now it greatly simplifies the flow
        //
        {
            std::string error;
            asio::spawn(_ios, [&](boost::asio::yield_context yield) {
                try
                {
                    if (config.bootstrap.empty())
                    {
                        #ifdef BEAM_DAPPNET
                        config.bootstrap.emplace_back("/ip4/3.16.160.95/tcp/38041/p2p/12D3KooWEFuqCDtMx5TQkQ4zHd4q38Ad4iE9zuCw6qGffah9WjEo");
                        LOG_INFO() << "Default DAPPNET IPFS bootstrap is used";
                        #elif defined(BEAM_TESTNET)
                        config.bootstrap.emplace_back("/dns4/eu-node01.testnet.beam.mw/tcp/38041/p2p/12D3KooWFEa2QaN5t3oTGurg1Fz5BkoE3ueHV18WxjHCXY16hHYM");
                        config.bootstrap.emplace_back("/dns4/eu-node02.testnet.beam.mw/tcp/38041/p2p/12D3KooWPrfHKa3Sc7qF96biwqy1JPRVDxoVhbxFtnfnbZQXVw8e");
                        config.bootstrap.emplace_back("/dns4/eu-node03.testnet.beam.mw/tcp/38041/p2p/12D3KooWF1oX1FP3chGQgzosCdNqBwSb37BPhM2fQJYiYMtGpHXt");
                        LOG_INFO() << "Default TESTNET IPFS bootstrap is used";
                        #elif defined(BEAM_MAINNET)
                        config.bootstrap.emplace_back("/dns4/eu-node01.mainnet.beam.mw/tcp/38041/p2p/12D3KooWJFduasQPYWhw4SsoFPmnJ1PXfmHYaA9qYKvn4JKM2hND");
                        config.bootstrap.emplace_back("/dns4/eu-node02.mainnet.beam.mw/tcp/38041/p2p/12D3KooWCjmtegxdSkkfutWqty39dwhEhYDWCDj6KCizDtft3sqc");
                        config.bootstrap.emplace_back("/dns4/eu-node03.mainnet.beam.mw/tcp/38041/p2p/12D3KooWL5c6JHHkfYLzBjcuot27eyKVhhczvvY617v1cy7QVUHt");
                        config.bootstrap.emplace_back("/dns4/eu-node04.mainnet.beam.mw/tcp/38041/p2p/12D3KooWHpgKQYXJMKXQZuwbuRoFK28cQLiVjCVFxhSpFX9XHNWZ");
                        LOG_INFO() << "Default MAINNET IPFS bootstrap is used";
                        #else
                        config.bootstrap.emplace_back("/ip4/3.19.32.148/tcp/38041/p2p/12D3KooWFrigFK9gVvCr7YDNNAAxDxmeyLDtR1tYvHcaXxuCcKpt");
                        LOG_INFO() << "Default MASTERNET IPFS bootstrap is used";
                        #endif
                    }
                    else
                    {
                        LOG_INFO() << "Custom IPFS bootstrap is provided";
                    }

                    if (config.peering.empty())
                    {
                        #ifdef BEAM_DAPPNET
                        config.peering.emplace_back("/ip4/3.16.160.95/tcp/38041/p2p/12D3KooWEFuqCDtMx5TQkQ4zHd4q38Ad4iE9zuCw6qGffah9WjEo");
                        LOG_INFO() << "Default DAPPNET IPFS peering is used";
                        #elif defined(BEAM_TESTNET)
                        config.peering.emplace_back("/dns4/eu-node01.testnet.beam.mw/tcp/38041/p2p/12D3KooWFEa2QaN5t3oTGurg1Fz5BkoE3ueHV18WxjHCXY16hHYM");
                        config.peering.emplace_back("/dns4/eu-node02.testnet.beam.mw/tcp/38041/p2p/12D3KooWPrfHKa3Sc7qF96biwqy1JPRVDxoVhbxFtnfnbZQXVw8e");
                        config.peering.emplace_back("/dns4/eu-node03.testnet.beam.mw/tcp/38041/p2p/12D3KooWF1oX1FP3chGQgzosCdNqBwSb37BPhM2fQJYiYMtGpHXt");
                        LOG_INFO() << "Default TESTNET IPFS peering is used";
                        #elif defined(BEAM_MAINNET)
                        config.peering.emplace_back("/dns4/eu-node01.mainnet.beam.mw/tcp/38041/p2p/12D3KooWJFduasQPYWhw4SsoFPmnJ1PXfmHYaA9qYKvn4JKM2hND");
                        config.peering.emplace_back("/dns4/eu-node02.mainnet.beam.mw/tcp/38041/p2p/12D3KooWCjmtegxdSkkfutWqty39dwhEhYDWCDj6KCizDtft3sqc");
                        config.peering.emplace_back("/dns4/eu-node03.mainnet.beam.mw/tcp/38041/p2p/12D3KooWL5c6JHHkfYLzBjcuot27eyKVhhczvvY617v1cy7QVUHt");
                        config.peering.emplace_back("/dns4/eu-node04.mainnet.beam.mw/tcp/38041/p2p/12D3KooWHpgKQYXJMKXQZuwbuRoFK28cQLiVjCVFxhSpFX9XHNWZ");
                        LOG_INFO() << "Default MAINNET IPFS peering is used";
                        #else
                        config.peering.emplace_back("/ip4/3.19.32.148/tcp/38041/p2p/12D3KooWFrigFK9gVvCr7YDNNAAxDxmeyLDtR1tYvHcaXxuCcKpt");
                        LOG_INFO() << "Default MASTERNET IPFS peering is used";
                        #endif
                    }
                    else
                    {
                        LOG_INFO() << "Custom IPFS peering is provided";
                    }

                    if (config.swarm_key.empty())
                    {
                        #ifdef BEAM_DAPPNET
                        config.swarm_key = "/key/swarm/psk/1.0.0/\n/base16/\nbf2f20636d7cd1c58c7ae6234ea056f6a673ffad71ec08af37405c4f3cbf9928";
                        LOG_INFO() << "Default DAPPNET IPFS swarm key would be used";
                        #elif defined(BEAM_TESTNET)
                        config.swarm_key = "/key/swarm/psk/1.0.0/\n/base16/\n1191aea7c9f99f679f477411d9d44f1ea0fdf5b42d995966b14a9000432f8c4a";
                        LOG_INFO() << "Default TESTNET IPFS swarm key would be used";
                        #elif defined(BEAM_MAINNET)
                        config.swarm_key = "/key/swarm/psk/1.0.0/\n/base16/\n1fabcf9eb018710a93a85214809b91a78b8ef5c49f84a5f72da3dff587b0aed5";
                        LOG_INFO() << "Default MAINNET IPFS swarm key would be used";
                        #else
                        config.swarm_key = "/key/swarm/psk/1.0.0/\n/base16/\n18502580a0f94a74eeb1bdd651e4235d0d9139b7baf3555716bc919619bb8ac4";
                        LOG_INFO() << "Default IPFS MASTERNET swarm key would be used";
                        #endif
                    }
                    else
                    {
                        LOG_INFO() << "Custom IPFS swarm_key is provided: " << config.swarm_key;
                    }

                    asio_ipfs::node::StateCB scb = [this](const std::string& error, uint32_t pcnt) {
                        _handler->AnyThread_onStatus(error, pcnt);
                    };

                    _node = asio_ipfs::node::build(_ios, scb, config, std::move(yield));
                    // TODO:IPFS lower connect timeout
                    // TODO:IPFS consider async launch
                    assert(_node);
                }
                catch (const boost::system::system_error &err)
                {
                    error = err2str(err);
                }
            });
            _ios.run();

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

            // since it has been running need to reset context
            _ios.reset();
        }

        //
        // Here we know that everything is OK, and we're ready for a real
        // threaded startup. Save data & spawn an infinitely running thread
        //
        _myid = _node->id();
        LOG_INFO() << "IPFS Service successfully started, ID is " << _myid;

        _ios_guard = std::make_unique<IOSGuard>(_ios.get_executor());
        _thread = MyThread([this, repo = config.repo_root]()
        {
            _ios.run();
        });
    }

    void IPFSService::ServiceThread_stop()
    {
        std::scoped_lock lock(_mutex);
        if (!_thread.joinable())
        {
            assert(false);
            throw std::runtime_error("IPFS service thread already stopped");
        }

        LOG_INFO() << "Stopping IPFS Service...";
        _node->free();
        _node.reset();
        _ios_guard->reset();
        _ios_guard.reset();

        assert(_thread.joinable());
        _thread.join();
        _ios.reset();
        LOG_INFO() << "IPFS Services stopped";
    }

    void IPFSService::AnyThread_add(std::vector<uint8_t>&& data, bool pin, uint32_t timeout, std::function<void (std::string&&)>&& res, Err&& err)
    {
        if (data.empty())
        {
            AnyThreaad_retErr(std::move(err), "Empty data buffer cannot be added to IPFS");
            return;
        }

        call_ipfs(timeout, std::move(res), std::move(err),[this, data = std::move(data), pin]
        (boost::asio::yield_context yield, std::function<void()>& cancel) -> auto
        {
            return _node->add(&data[0], data.size(), pin, cancel, std::move(yield));
        });
    }

    void IPFSService::AnyThread_hash(std::vector<uint8_t>&& data, uint32_t timeout, std::function<void (std::string&&)>&& res, Err&& err)
    {
        if (data.empty())
        {
            AnyThreaad_retErr(std::move(err), "Empty data buffer cannot be hashed");
            return;
        }

        call_ipfs(timeout, std::move(res), std::move(err),[this, data = std::move(data)]
        (boost::asio::yield_context yield, std::function<void()>& cancel) -> auto
        {
            return _node->calc_cid(&data[0], data.size(), cancel, std::move(yield));
        });
    }

    void IPFSService::AnyThread_get(const std::string& hash, uint32_t timeout, std::function<void (std::vector<uint8_t>&&)>&& res, Err&& err)
    {
        if (hash.empty())
        {
            AnyThreaad_retErr(std::move(err), "Cannot get data via an empty hash");
            return;
        }

        call_ipfs(timeout, std::move(res), std::move(err), [this, hash]
        (boost::asio::yield_context yield, std::function<void()>& cancel) -> auto
        {
            return _node->cat(hash, cancel, std::move(yield));
        });
    }

    void IPFSService::AnyThread_pin(const std::string& hash, uint32_t timeout, std::function<void ()>&& res, Err&& err)
    {
        if (hash.empty())
        {
            AnyThreaad_retErr(std::move(err), "Cannot get data via an empty hash");
            return;
        }

        call_ipfs(timeout, std::move(res), std::move(err), [this, hash]
        (boost::asio::yield_context yield, std::function<void()>& cancel) -> JustVoid
        {
            _node->pin(hash, cancel, std::move(yield));
            return JustVoid{};
        });
    }

    void IPFSService::AnyThread_unpin(const std::string& hash, std::function<void ()>&& res, Err&& err)
    {
        if (hash.empty())
        {
            AnyThreaad_retErr(std::move(err), "Cannot unpin an empty hash");
            return;
        }

        call_ipfs(0, std::move(res), std::move(err), [this, hash]
        (boost::asio::yield_context yield, std::function<void()>& cancel) -> JustVoid
        {
            _node->unpin(hash, cancel, std::move(yield));
            return JustVoid{};
        });
    }

    void IPFSService::AnyThread_gc(uint32_t timeout, std::function<void ()>&& res, Err&& err)
    {
        call_ipfs(timeout, std::move(res), std::move(err), [this]
        (boost::asio::yield_context yield, std::function<void()>& cancel) -> JustVoid
        {
            _node->gc(cancel, std::move(yield));
            return JustVoid{};
        });
    }

    void IPFSService::AnyThreaad_retToClient(std::function<void()>&& what)
    {
        _handler->AnyThread_pushToClient(std::move(what));
    }
}
