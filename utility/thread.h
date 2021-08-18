// Copyright 2018-2021 The Beam Team
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
#include <functional>
#include <queue>
#include <mutex>
#include <condition_variable>
#include "helpers.h"

namespace beam
{
	class SimpleThreadPool
	{
	public:
		using Task = std::function<void()>;

		SimpleThreadPool(size_t threads)
		{
			m_Threads.reserve(threads);
			for (size_t i = 0; i < threads; ++i)
			{
				m_Threads.push_back(std::thread(&SimpleThreadPool::DoWork, this));
			}
		}

		~SimpleThreadPool()
		{
			Stop();
			for (auto& t : m_Threads)
			{
				if (t.joinable())
				{
					t.join();
				}
			}
		}

		void Push(Task&& task)
		{
			std::unique_lock lock(m_Mutex);
			m_Tasks.push(std::move(task));
			m_NewTask.notify_one();
		}

		void Stop()
		{
			std::unique_lock lock(m_Mutex);
			m_Shutdown = true;
			m_NewTask.notify_all();
		}

		size_t GetPoolSize() const
		{
			return m_Threads.size();
		}

	private:

		void DoWork()
		{
			while (true)
			{
				Task t;
				{
					std::unique_lock lock(m_Mutex);
					m_NewTask.wait(lock, [&] { return m_Shutdown || !m_Tasks.empty(); });
					if (m_Shutdown)
						break;

					t = std::move(m_Tasks.front());
					m_Tasks.pop();
				}
				try
				{
					t();
				}
				catch (const std::exception& ex)
				{
					std::cout << "Exception: " << ex.what() << std::endl;
				}
				catch (...)
				{
					std::cout << "Exception"<< std::endl;
				}
			}
		}

	private:
		std::vector<std::thread> m_Threads;
		std::queue<Task> m_Tasks;
		std::mutex m_Mutex;
		std::condition_variable m_NewTask;
		bool m_Shutdown = false;
	};

	class PoolThread
	{

		struct IdControl
		{
			std::thread::id m_ID = {};
			mutable std::mutex m_Mutex;
			std::condition_variable m_cv;
			enum struct State
			{
				Unassigned,
				Attached,
				Completed,
				Detached,
				Joined
			};
			State m_State = State::Unassigned;

			void StoreID(std::thread::id id)
			{
				std::unique_lock lock(m_Mutex);
				if (m_State == State::Unassigned)
				{
					m_ID = id;
					m_State = State::Attached;
					m_cv.notify_one();
				}
			}

			void Complete()
			{
				std::unique_lock lock(m_Mutex);
				if (m_State == State::Attached)
				{
					m_State = State::Completed;
					m_cv.notify_one();
				}
			}

			std::thread::id GetID() const
			{
				std::unique_lock lock(m_Mutex);
				return m_ID;
			}

			void Join()
			{
				std::unique_lock lock(m_Mutex);
				m_cv.wait(lock, [&]() {return m_State == State::Completed; });
				m_State = State::Joined;
				m_ID = {};
			}

			bool IsJoinable() const
			{
				std::unique_lock lock(m_Mutex);
				return m_State != State::Detached && m_State != State::Joined;
			}

			void Detach()
			{
				std::unique_lock lock(m_Mutex);
				m_State = State::Detached;
				m_ID = {};
			}
		};

	public:

		PoolThread() noexcept
			: m_ID(std::make_shared<IdControl>())
		{

		}

		template<typename Func, typename... Args>
		explicit PoolThread(Func&& f, Args&&... args)
			: PoolThread()
		{
			using Params = std::tuple<std::decay_t<Func>, std::decay_t<Args>...>;
			using Indecies = std::make_index_sequence<1 + sizeof...(Args)>;
			auto params = std::make_shared<Params>(std::forward<Func>(f), std::forward<Args>(args)...);
			s_threadPool.Push([sp = m_ID, params]()
			{
				sp->StoreID(std::this_thread::get_id());
				MyInvoke(*params, Indecies{});
				sp->Complete();
			});
		}

		PoolThread(PoolThread&& other) noexcept
			: m_ID(std::exchange(other.m_ID, {}))
		{

		}

		PoolThread& operator=(PoolThread&& other) noexcept
		{
			assert(get_id() == std::thread::id());
			m_ID = std::exchange(other.m_ID, {});
			return *this;
		}

		~PoolThread() noexcept
		{
			if (joinable()) 
			{
				std::terminate();
			}
		}

		PoolThread(const PoolThread&) = delete;
		PoolThread& operator=(const PoolThread&) = delete;

		std::thread::id get_id() const noexcept
		{
			assert(m_ID);
			return m_ID->GetID();
		}

		bool joinable() const noexcept
		{
			return m_ID && m_ID->IsJoinable();
		}

		void join()
		{
			assert(m_ID);
			m_ID->Join();
		}

		void detach()
		{
			assert(m_ID);
			m_ID->Detach();
		}

		[[nodiscard]] static unsigned int hardware_concurrency() noexcept 
		{
			return static_cast<unsigned int>(s_threadPool.GetPoolSize());
		}

	private:

		template<typename Params, size_t... I>
		static void MyInvoke(const Params& p, std::index_sequence<I...>)
		{
			std::invoke(std::get<I>(p)...);
		}

		static size_t GetCoresNum()
		{
#ifdef BEAM_WEB_WALLET_THREADS_NUM
			auto s = static_cast<size_t>(BEAM_WEB_WALLET_THREADS_NUM);
			if (std::thread::hardware_concurrency() >= s)
				return s;
#endif // BEAM_WEB_WALLET_THREADS_NUM

			return  std::thread::hardware_concurrency();
		}

	private:
		inline static SimpleThreadPool s_threadPool{ GetCoresNum() };
		std::shared_ptr<IdControl> m_ID;
	};
#if defined __EMSCRIPTEN__
	using MyThread = PoolThread;
#else
	using MyThread = std::thread;
#endif
}