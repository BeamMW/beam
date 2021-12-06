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
#include "asio-ipfs/include/asio_ipfs/node.h"
#include "utility/logger.h"
#include <boost/asio/spawn.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

namespace beam::wallet::imp
{
    class IPFSService: public beam::wallet::IPFSService
    {
    public:
        using HandlerPtr = beam::wallet::IPFSService::HandlerPtr;

        explicit IPFSService(HandlerPtr);
        ~IPFSService() override;

        bool running() const override;
        void start(const std::string& storagePath) override;
        void stop() override;
        void add(std::vector<uint8_t>&& data, std::function<void (std::string&&)>&& res, Err&&) override;
        void get(const std::string& hash, uint32_t timeout, std::function<void (std::vector<uint8_t>&&)>&& res, Err&&) override;
        void pin(const std::string& hash, uint32_t timeout, std::function<void ()>&& res, Err&&) override;
        void unpin(const std::string& hash, uint32_t timeout, std::function<void ()>&& res, Err&&) override;
        void gc(uint32_t timeout, std::function<void ()>&& res, Err&&) override;

        [[nodiscard]] std::string id() const override
        {
            return _myid;
        }

    private:
        struct JustVoid {};

        template<typename TA, typename TR>
        void call_ipfs(uint32_t timeout, TR&& res, Err&& err, TA&& action)
        {
            if(!_node || !_thread)
            {
                retErr(std::move(err), "Unexpected add call. IPFS is not started");
                return;
            }

            std::shared_ptr<boost::asio::deadline_timer> deadline;
            if (timeout)
            {
                deadline = std::make_shared<boost::asio::deadline_timer>(
                        *_ios, boost::posix_time::milliseconds(timeout)
                );
            }

            boost::asio::spawn(*_ios, [this,
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
                            deadline->async_wait([&cancel, err, this](const boost::system::error_code& ec) mutable {
                                if (ec == boost::asio::error::operation_aborted)
                                {
                                    // Timer cancelled
                                }
                                else
                                {
                                    retErr(std::move(err), "operation timed out");
                                }
                            });
                        }

                        auto result = action(yield, cancel);
                        if (deadline)
                        {
                            deadline->cancel();
                        }
                        retVal(std::move(res), std::move(result));
                    }
                    catch(const boost::system::system_error& se)
                    {
                        retErr(std::move(err), err2str(se));
                    }
                }
            );
        }

        // do not change this to varargs & bind
        // a bit verbose to call but caller would always
        // copy params to lambda if any present and
        // won't pass local vars by accident
        void retToClient(std::function<void ()>&& what);

        template<typename T1, typename T2>
        void retVal(T1&& func, T2&& what)
        {
            retToClient([func = std::forward<T1>(func), what = std::forward<T2>(what)]() mutable {
                func(std::move(what));
            });
        }

        void retErr(Err&& err, std::string&& what)
        {
            retVal(std::move(err), std::move(what));
        }

        std::string err2str(const boost::system::system_error &err)
        {
            std::stringstream ss;
            ss << err.code() << ", " << err.what();
            return ss.str();
        }

        std::string _path;
        std::string _myid;
        std::unique_ptr<asio_ipfs::node> _node;

        //
        // Threading & async stuff
        //
        HandlerPtr _handler;
        std::unique_ptr<MyThread> _thread;
        std::unique_ptr<boost::asio::io_context> _ios;

        typedef boost::asio::executor_work_guard<decltype(_ios->get_executor())> IOSGuard;
        std::unique_ptr<IOSGuard> _ios_guard;
    };

    template<>
    inline void IPFSService::retVal(std::function<void()>&& func, IPFSService::JustVoid&& what)
    {
        retToClient([func = std::move(func)] {
            func();
        });
    }
}
