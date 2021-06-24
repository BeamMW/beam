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

namespace beam
{
	class SimpleThreadPool
	{
	public:
		using Task = std::function<void()>;

		SimpleThreadPool(size_t threads)
		{
			m_Threads.resize(threads);
			for (auto& t : m_Threads)
			{
				t = std::thread(&SimpleThreadPool::DoWork, this);
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
				t();
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
			bool m_RunToCompletion = false;

			void StoreID(std::thread::id id)
			{
				std::unique_lock lock(m_Mutex);
				m_ID = id;
				m_cv.notify_one();
			}

			void Complete()
			{
				std::unique_lock lock(m_Mutex);
				assert(!m_RunToCompletion);
				m_RunToCompletion = true;
				m_cv.notify_one();
			}

			std::thread::id GetID() const
			{
				std::unique_lock lock(m_Mutex);
				return m_ID;
			}

			void Join()
			{
				std::unique_lock lock(m_Mutex);
				m_cv.wait(lock, [&]() {return m_RunToCompletion == true; });
				m_ID = {};
			}

			void Detach()
			{
				m_ID = {};
			}
		};

	public:

		PoolThread() noexcept
			: m_ID(std::make_unique<IdControl>())
		{

		}

		template<typename Func, typename... Args>
		explicit PoolThread(Func&& f, Args&&... args)
			: PoolThread()
		{
			using Params = std::tuple<std::decay_t<Func>, std::decay_t<Args>...>;
			using Indecies = std::make_index_sequence<1 + sizeof...(Args)>;
			auto params = std::make_shared<Params>(std::forward<Func>(f), std::forward<Args>(args)...);

			s_threadPool.Push([id = m_ID.get(), params]()
			{
				id->StoreID(std::this_thread::get_id());
				MyInvoke(*params, Indecies{});
				id->Complete();
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

		PoolThread(const PoolThread&) = delete;
		PoolThread& operator=(const PoolThread&) = delete;

		std::thread::id get_id() const noexcept
		{
			assert(m_ID);
			return m_ID->GetID();
		}

		bool joinable() const noexcept
		{
			return true;
		}

		void join()
		{
			assert(m_ID);
			m_ID->Join();
		}

		void detach()
		{
			m_ID->Detach();
		}

	private:

		template<typename Params, size_t... I>
		static void MyInvoke(const Params& p, std::index_sequence<I...>)
		{
			std::invoke(std::get<I>(p)...);
		}

	private:
		inline static SimpleThreadPool s_threadPool{ std::thread::hardware_concurrency() };
		std::unique_ptr<IdControl> m_ID;
	};
#if defined __EMSCRIPTEN__
	using MyThread = PoolThread;
#else
	using MyThread = std::thread;
#endif
}