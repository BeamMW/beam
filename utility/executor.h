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

#include "common.h"
#include <condition_variable>
#include <thread>
#include <boost/intrusive/list.hpp>
#include "thread.h"

namespace beam
{
	// parallel context-free execution
	struct Executor
	{
		static thread_local Executor* s_pInstance;

		struct Scope
		{
			Executor* m_pPrev;

			Scope(Executor& ex) {
				m_pPrev = s_pInstance;
				s_pInstance = &ex;
			}
			~Scope() {
				s_pInstance = m_pPrev;
			}
		};

		// Per-thread context, which tasks may access
		struct Context
		{
			Executor* m_pThis;
			uint32_t m_iThread;

			void get_Portion(uint32_t& i0, uint32_t& nCount, uint32_t nTotal);
		private:
			static uint32_t get_Pos(uint32_t nTotal, uint32_t iThread, uint32_t nThreads);
		};

		struct TaskSync {
			virtual void Exec(Context&) = 0;
		};

		struct TaskAsync
			:public boost::intrusive::list_base_hook<>
			, public TaskSync
		{
			typedef std::unique_ptr<TaskAsync> Ptr;
			virtual ~TaskAsync() {}
		};

		virtual uint32_t get_Threads() = 0;
		virtual void Push(TaskAsync::Ptr&&) = 0;
		virtual uint32_t Flush(uint32_t nMaxTasks = 0) = 0;
		virtual void ExecAll(TaskSync&) = 0;
		virtual ~Executor() = default;
	};

	// standard multi-threaded executor. All threads are created with default stack and priority
	struct ExecutorMT
		:public Executor
	{
		virtual uint32_t get_Threads() override;
		virtual void Push(TaskAsync::Ptr&&) override;
		virtual uint32_t Flush(uint32_t nMaxTasks) override;
		virtual void ExecAll(TaskSync&) override;

		ExecutorMT();
		~ExecutorMT() { Stop(); }
		void Stop();

		void set_Threads(uint32_t);

	protected:

		uint32_t m_Threads; // set at c'tor to num of cores.

		virtual void StartThread(MyThread&, uint32_t iThread) = 0;

		void RunThreadCtx(Context&);

	private:
		std::mutex m_Mutex;

		boost::intrusive::list<TaskAsync> m_queTasks;

		uint32_t m_InProgress;
		uint32_t m_FlushTarget;
		bool m_Run;
		TaskSync* m_pCtl;
		std::condition_variable m_NewTask;
		std::condition_variable m_Flushed;

		std::vector<MyThread> m_vThreads;

		void InitSafe();
		void FlushLocked(std::unique_lock<std::mutex>&, uint32_t nMaxTasks);
		void RunThreadInternal(uint32_t);
	};
}
