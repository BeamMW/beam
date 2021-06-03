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
		uint32_t m_Freeze = 0; // incremented when we're awaiting something
		void Unfreeze();
		void OnUnfreezed();

		struct UnfreezeEvt
			:public io::IdleEvt
		{
			virtual void OnSchedule() override;
			IMPLEMENT_GET_PARENT_OBJ(ManagerStd, m_UnfreezeEvt)
		} m_UnfreezeEvt;

		struct RemoteRead;

		void RunSync();
		bool PerformRequestSync(proto::FlyClient::Request&);

	protected:
		Height get_Height() override;
		bool get_HdrAt(Block::SystemState::Full&) override;
		void VarsEnum(const Blob& kMin, const Blob& kMax, IReadVars::Ptr&) override;
		void LogsEnum(const Blob& kMin, const Blob& kMax, const HeightPos* pPosMin, const HeightPos* pPosMax, IReadLogs::Ptr&) override;
		void DerivePk(ECC::Point& pubKey, const ECC::Hash::Value& hv) override;
		void GenerateKernel(const ContractID* pCid, uint32_t iMethod, const Blob& args, const Shaders::FundsChange* pFunds, uint32_t nFunds, const ECC::Hash::Value* pSig, uint32_t nSig, const char* szComment, uint32_t nCharge) override;
		bool get_SpecialParam(const char*, Blob&) override;
		bool VarGetProof(Blob& key, ByteBuffer& val, beam::Merkle::Proof&) override;
		bool LogGetProof(const HeightPos&, beam::Merkle::Proof&) override;

		virtual void OnDone(const std::exception* pExc) {}

	public:

		ManagerStd();

		// Params
		proto::FlyClient::INetwork::Ptr m_pNetwork; // required for 'view' operations
		Key::IPKdf::Ptr m_pPKdf; // required for user-related info (account-specific pubkeys, etc.)
		Block::SystemState::IHistory* m_pHist = nullptr;

		ByteBuffer m_BodyManager; // always required
		ByteBuffer m_BodyContract; // required if creating a new contract

		// results
		std::ostringstream m_Out;
		ContractInvokeData m_vInvokeData;

		void StartRun(uint32_t iMethod);
	};
} // namespace

