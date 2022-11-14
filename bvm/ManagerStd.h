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

#include "bvm2.h"
#include "../core/fly_client.h"
#include "invoke_data.h"

namespace beam::bvm2 {
	class ManagerStd
		:public ProcessorManager
	{
	protected:

		struct Pending
		{
			struct IBase
			{
				virtual ~IBase() {}
				typedef std::unique_ptr<IBase> Ptr;
			};

			IBase* m_pBlocker = nullptr;

			IBase::Ptr m_pSingleRequest;
			IBase::Ptr m_pCommMsg;

			void OnDone(IBase&);

			IMPLEMENT_GET_PARENT_OBJ(ManagerStd, m_Pending)
		} m_Pending;

		void OnUnfreezed();

		struct UnfreezeEvt
			:public io::IdleEvt
		{
			virtual void OnSchedule() override;
			IMPLEMENT_GET_PARENT_OBJ(ManagerStd, m_UnfreezeEvt)
		} m_UnfreezeEvt;

		struct RemoteRead;

		void SetParentContext(std::unique_ptr<beam::Merkle::Hash>& pTrg) const;
		void PerformSingleRequest(proto::FlyClient::Request& r);
		proto::FlyClient::Request::Ptr GetResSingleRequest();

		void RunSync();
		void Comm_OnNewMsg(const Blob&, Comm::Channel&);
		void Comm_OnNewMsg();

	protected:
		void SelectContext(bool bDependent, uint32_t nChargeNeeded) override;
		bool get_HdrAt(Block::SystemState::Full&) override;
		void VarsEnum(const Blob& kMin, const Blob& kMax, IReadVars::Ptr&) override;
		void LogsEnum(const Blob& kMin, const Blob& kMax, const HeightPos* pPosMin, const HeightPos* pPosMax, IReadLogs::Ptr&) override;
		bool get_AssetInfo(Asset::Full&) override;
		void get_ContractShader(ByteBuffer&) override;
		bool get_SpecialParam(const char*, Blob&) override;
		bool VarGetProof(Blob& key, ByteBuffer& val, beam::Merkle::Proof&) override;
		bool LogGetProof(const HeightPos&, beam::Merkle::Proof&) override;
		void Comm_Wait(uint32_t nTimeout_ms) override;

		virtual void OnDone(const std::exception* pExc) {}
		virtual void OnReset();
		virtual bool IsSuspended() override;

	public:

		ManagerStd();

		// Params
		proto::FlyClient::INetwork::Ptr m_pNetwork; // required for 'view' operations
		Block::SystemState::IHistory* m_pHist = nullptr;
		bool m_EnforceDependent = false;

		ByteBuffer m_BodyManager; // always required
		ByteBuffer m_BodyContract; // required if creating a new contract

		// results
		std::ostringstream m_Out;

		void StartRun(uint32_t iMethod);
	};
} // namespace

