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
#pragma once

#include "ipfs.h"
#include "ipfs_async.h"
#include <asio-ipfs/include/asio_ipfs.h>
#include "utility/logger.h"
#include <boost/asio/spawn.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <shared_mutex>

namespace beam::wallet::imp
{
    class IPFSService: public beam::wallet::IPFSService
    {
    public:
        using HandlerPtr = beam::wallet::IPFSService::HandlerPtr;

        explicit IPFSService(HandlerPtr);
        ~IPFSService() override;

        void ServiceThread_start(asio_ipfs::config config) override;
        void ServiceThread_stop() override;

        void AnyThread_add(std::vector<uint8_t>&& data, bool pin, uint32_t timeout,
                           std::function<void (std::string&&)>&& res, Err&&) override;

        void AnyThread_hash(std::vector<uint8_t>&& data, uint32_t timeout,
                            std::function<void (std::string&&)>&& res, Err&&) override;

        void AnyThread_get(const std::string& hash, uint32_t timeout,
                           std::function<void (std::vector<uint8_t>&&)>&& res, Err&&) override;

        void AnyThread_pin(const std::string& hash, uint32_t timeout,
                           std::function<void ()>&& res, Err&&) override;

        void AnyThread_unpin(const std::string& hash, std::function<void ()>&& res, Err&&) override;
        void AnyThread_gc(uint32_t timeout, std::function<void ()>&& res, Err&&) override;

        [[nodiscard]] std::string AnyThread_id() const override
        {
            std::scoped_lock lock(_mutex);
            return _myid;
        }

        [[nodiscard]] bool AnyThread_running() const override
        {
            std::scoped_lock lock(_mutex);
            return _thread.joinable();
        }

    private:
        struct JustVoid {};

        template<typename TA, typename TR>
        void call_ipfs(uint32_t timeout, TR&& res, Err&& err, TA&& action)
        {
            std::scoped_lock lock(_mutex);
            if(!_thread.joinable())
            {
                AnyThreaad_retErr(std::move(err), "Unexpected add call. IPFS is not started");
                return;
            }

            std::shared_ptr<boost::asio::deadline_timer> deadline;
            if (timeout)
            {
                deadline = std::make_shared<boost::asio::deadline_timer>(
                        _ios, boost::posix_time::milliseconds(timeout)
                );
            }

            boost::asio::spawn(_ios, [this,
                                       err = std::move(err),
                                       deadline = std::move(deadline),
                                       action = std::forward<TA>(action),
                                       res = std::forward<TR>(res)
                                      ]
                (boost::asio::yield_context yield) mutable {
                    try
                    {
                        std::function<void ()> cancel;
                        if (deadline)
                        {
                            deadline->async_wait([err, this](const boost::system::error_code& ec) mutable {
                                if (ec == boost::asio::error::operation_aborted)
                                {
                                    // Timer cancelled
                                }
                                else
                                {
                                    AnyThreaad_retErr(std::move(err), "operation timed out");
                                }
                            });
                        }

                        auto result = action(yield, cancel);
                        if (deadline)
                        {
                            deadline->cancel();
                        }
                        AnyThreaad_retVal(std::move(res), std::move(result));
                    }
                    catch(const boost::system::system_error& se)
                    {
                        AnyThreaad_retErr(std::move(err), err2str(se));
                    }
                }
            );
        }

        // do not change this to varargs & bind
        // a bit verbose to call but caller would always
        // copy params to lambda if any present and
        // won't pass local vars by accident
        void AnyThreaad_retToClient(std::function<void ()>&& what);

        template<typename T1, typename T2>
        void AnyThreaad_retVal(T1&& func, T2&& what)
        {
            AnyThreaad_retToClient([func = std::forward<T1>(func), what = std::forward<T2>(what)]() mutable {
                func(std::move(what));
            });
        }

        void AnyThreaad_retErr(Err&& err, std::string&& what)
        {
            AnyThreaad_retVal(std::move(err), std::move(what));
        }

        std::string err2str(const boost::system::system_error &err)
        {
            std::stringstream ss;
            ss << err.code() << ", " << err.what();
            return ss.str();
        }

        std::unique_ptr<asio_ipfs::node> _node;
        std::string _myid;

        //
        // Threading & async stuff
        //
        HandlerPtr _handler;
        MyThread _thread;
        boost::asio::io_context _ios;

        typedef boost::asio::executor_work_guard<decltype(_ios.get_executor())> IOSGuard;
        std::unique_ptr<IOSGuard> _ios_guard;

        // Since we usually have only 1 thread that uses IPFSService + multiple IPFS threads
        // that do not require lock on `this` recursive_mutex is OK.
        // Shared (read/write) recursive mutex would fit better for real multithreading for client
        // side, but there is no such beast in std::
        mutable std::recursive_mutex _mutex;
    };

    template<>
    inline void IPFSService::AnyThreaad_retVal(std::function<void()>&& func, IPFSService::JustVoid&& what)
    {
        AnyThreaad_retToClient([func = std::move(func)] {
            func();
        });
    }
}
