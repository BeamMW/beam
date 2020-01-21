// Copyright 2019 The Beam Team
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

#include "private_key_keeper.h"

namespace beam::wallet
{

	////////////////////////////////
	// Synchronous methods implemented via asynchronous
	struct IPrivateKeyKeeper2::HandlerSync
		:public Handler
	{
		Status::Type m_Status = Status::InProgress;

		virtual void OnDone(Status::Type nRes) override
		{
			m_Status = nRes;
			io::Reactor::get_Current().stop();
		}

		Status::Type Wait()
		{
			io::Reactor::get_Current().run();
			return m_Status;
		}
	};


	template <typename TMethod>
	IPrivateKeyKeeper2::Status::Type IPrivateKeyKeeper2::InvokeSyncInternal(TMethod& m)
	{
		struct MyHandler
			:public HandlerSync
		{
			TMethod m_M;
			virtual ~MyHandler() {}
		};

 		std::shared_ptr<MyHandler> p(new MyHandler);
		p->m_M = std::move(m);

		InvokeAsync(p->m_M, p);
		Status::Type ret = p->Wait();

		if (Status::InProgress != ret)
			m = std::move(p->m_M);

		return ret;

	}

#define THE_MACRO(method) \
	IPrivateKeyKeeper2::Status::Type IPrivateKeyKeeper2::InvokeSync(Method::method& m) \
	{ \
		return InvokeSyncInternal(m); \
	}

	KEY_KEEPER_METHODS(THE_MACRO)
#undef THE_MACRO

	////////////////////////////////
	// Asynchronous methods implemented via synchronous
#define THE_MACRO(method) \
	void IPrivateKeyKeeper2::InvokeAsync(Method::method& m, const Handler::Ptr& p) \
	{ \
		Status::Type res = InvokeSync(m); \
		p->OnDone(res); \
	}

	KEY_KEEPER_METHODS(THE_MACRO)
#undef THE_MACRO

} // namespace beam::wallet
