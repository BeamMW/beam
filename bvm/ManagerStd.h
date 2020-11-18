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

namespace beam {
namespace bvm2 {

	struct FundsMap
		:public std::map<Asset::ID, AmountSigned>
	{
		void AddSpend(Asset::ID aid, AmountSigned val);
		void operator += (const FundsMap&);
	};

	struct ContractInvokeData
	{
		ECC::uintBig m_Cid;
		uint32_t m_iMethod;
		ByteBuffer m_Data;
		ByteBuffer m_Args;
		std::vector<ECC::Hash::Value> m_vSig;
		Amount m_Fee;
		FundsMap m_Spend; // ins - outs, not including fee

		template <typename Archive>
		void serialize(Archive& ar)
		{
			ar
				& m_iMethod
				& m_Args
				& m_vSig
				& m_Fee
				& Cast::Down< std::map<Asset::ID, AmountSigned> >(m_Spend);

			if (m_iMethod)
				ar & m_Cid;
			else
			{
				m_Cid = Zero;
				ar & m_Data;
			}
		}

		void Generate(Transaction&, Key::IKdf&, const HeightRange& hr) const;
	};


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

		// params readout
		struct VarsRead
			:public proto::FlyClient::Request::IHandler
		{
			struct Request
				:public proto::FlyClient::RequestContractVars
			{
				typedef boost::intrusive_ptr<Request> Ptr;

				virtual ~Request() {}

				size_t m_Consumed;
			};

			Request::Ptr m_pRequest;

			~VarsRead() { Abort(); }
			void Abort();

			virtual void OnComplete(proto::FlyClient::Request&) override;

			IMPLEMENT_GET_PARENT_OBJ(ManagerStd, m_VarsRead)
		} m_VarsRead;

		void RunSync();

	protected:
		Height get_Height() override;
		bool get_HdrAt(Block::SystemState::Full&);
		void VarsEnum(const Blob& kMin, const Blob& kMax) override;
		bool VarsMoveNext(Blob& key, Blob& val) override;
		void DerivePk(ECC::Point& pubKey, const ECC::Hash::Value& hv) override;
		void GenerateKernel(const ContractID* pCid, uint32_t iMethod, const Blob& args, const Shaders::FundsChange* pFunds, uint32_t nFunds, const ECC::Hash::Value* pSig, uint32_t nSig, Amount nFee) override;

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
		std::vector<ContractInvokeData> m_vInvokeData;

		void StartRun(uint32_t iMethod);
	};


} // namespace bvm2
} // namespace beam
