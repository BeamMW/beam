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

namespace beam::bvm2
{

	struct ContractTestProcessor
		:public ProcessorContract
	{
		BlobMap::Set m_Vars;

		struct Action
			:public boost::intrusive::list_base_hook<>
		{
			virtual ~Action() = default;
			virtual void Undo(ContractTestProcessor&) = 0;

			typedef intrusive::list_autoclear<Action> List;
		};

		Action::List m_lstUndo;

		void UndoChanges(size_t nTrg = 0)
		{
			while (m_lstUndo.size() > nTrg)
			{
				auto& x = m_lstUndo.back();
				x.Undo(*this);
				m_lstUndo.Delete(x);
			}
		}


		virtual void LoadVar(const VarKey& vk, uint8_t* pVal, uint32_t& nValInOut) override
		{
			auto* pE = m_Vars.Find(Blob(vk.m_p, vk.m_Size));
			if (pE && !pE->m_Data.empty())
			{
				auto n0 = static_cast<uint32_t>(pE->m_Data.size());
				memcpy(pVal, &pE->m_Data.front(), std::min(n0, nValInOut));
				nValInOut = n0;
			}
			else
				nValInOut = 0;
		}

		virtual void LoadVar(const VarKey& vk, ByteBuffer& res) override
		{
			auto* pE = m_Vars.Find(Blob(vk.m_p, vk.m_Size));
			if (pE)
				res = pE->m_Data;
			else
				res.clear();
		}

		virtual uint32_t SaveVar(const VarKey& vk, const uint8_t* pVal, uint32_t nVal) override
		{
			return SaveVar(Blob(vk.m_p, vk.m_Size), pVal, nVal);
		}

		struct Action_Var
			:public Action
		{
			ByteBuffer m_Key;
			ByteBuffer m_Value;

			virtual void Undo(ContractTestProcessor& p) override
			{
				p.SaveVar2(m_Key, m_Value.empty() ? nullptr : &m_Value.front(), static_cast<uint32_t>(m_Value.size()), nullptr);
			}
		};

		uint32_t SaveVar(const Blob& key, const uint8_t* pVal, uint32_t nVal)
		{
			auto pUndo = std::make_unique<Action_Var>();
			uint32_t nRet = SaveVar2(key, pVal, nVal, pUndo.get());

			m_lstUndo.push_back(*pUndo.release());
			return nRet;
		}

		uint32_t SaveVar2(const Blob& key, const uint8_t* pVal, uint32_t nVal, Action_Var* pAction)
		{
			auto* pE = m_Vars.Find(key);
			auto nOldSize = pE ? static_cast<uint32_t>(pE->m_Data.size()) : 0;

			if (pAction)
			{
				key.Export(pAction->m_Key);
				if (pE)
					pAction->m_Value.swap(pE->m_Data);
			}

			if (nVal)
			{
				if (!pE)
					pE = m_Vars.Create(key);

				Blob(pVal, nVal).Export(pE->m_Data);
			}
			else
			{
				if (pE)
					m_Vars.Delete(*pE);
			}

			return nOldSize;
		}

		Height m_Height = 0;
		Height get_Height() override { return m_Height; }

		bool get_HdrAt(Block::SystemState::Full& s) override
		{
			Height h = s.m_Height;
			if (h > m_Height)
				return false;

			ZeroObject(s);
			s.m_Height = h;
			return true;
		}

		struct AssetData {
			Amount m_Amount;
			PeerID m_Pid;
		};
		typedef std::map<Asset::ID, AssetData> AssetMap;
		AssetMap m_Assets;

		virtual Asset::ID AssetCreate(const Asset::Metadata& md, const PeerID& pid) override
		{
			Asset::ID aid = AssetCreate2(md, pid);
			if (aid)
			{
				struct MyAction
					:public Action
				{
					Asset::ID m_Aid;

					virtual void Undo(ContractTestProcessor& p) override {
						auto it = p.m_Assets.find(m_Aid);
						verify_test(p.m_Assets.end() != it);
						p.m_Assets.erase(it);
					}
				};

				auto pUndo = std::make_unique<MyAction>();
				pUndo->m_Aid = aid;
				m_lstUndo.push_back(*pUndo.release());
			}

			return aid;
		}

		Asset::ID AssetCreate2(const Asset::Metadata&, const PeerID& pid)
		{
			Asset::ID ret = 1;
			while (m_Assets.find(ret) != m_Assets.end())
				ret++;

			auto& val = m_Assets[ret];
			val.m_Amount = 0;
			val.m_Pid = pid;

			return ret;
		}

		virtual bool AssetEmit(Asset::ID aid, const PeerID& pid, AmountSigned val) override
		{
			bool bRet = AssetEmit2(aid, pid, val);
			if (bRet)
			{
				struct MyAction
					:public Action
				{
					Asset::ID m_Aid;
					AmountSigned m_Val;

					virtual void Undo(ContractTestProcessor& p) override {
						auto it = p.m_Assets.find(m_Aid);
						verify_test(p.m_Assets.end() != it);
						it->second.m_Amount -= m_Val;;
					}
				};

				auto pUndo = std::make_unique<MyAction>();
				pUndo->m_Aid = aid;
				pUndo->m_Val = val;
				m_lstUndo.push_back(*pUndo.release());
			}

			return bRet;
		}

		bool AssetEmit2(Asset::ID aid, const PeerID& pid, AmountSigned val)
		{
			auto it = m_Assets.find(aid);
			if (m_Assets.end() == it)
				return false;

			auto& x = it->second;
			if (x.m_Pid != pid)
				return false;

			x.m_Amount += val; // don't care about overflow
			return true;
		}

		virtual bool AssetDestroy(Asset::ID aid, const PeerID& pid) override
		{
			bool bRet = AssetDestroy2(aid, pid);
			if (bRet)
			{
				struct MyAction
					:public Action
				{
					Asset::ID m_Aid;
					PeerID m_Pid;

					virtual void Undo(ContractTestProcessor& p) override {
						auto& x = p.m_Assets[m_Aid];
						x.m_Amount = 0;
						x.m_Pid = m_Pid;
					}
				};

				auto pUndo = std::make_unique<MyAction>();
				pUndo->m_Aid = aid;
				pUndo->m_Pid = pid;
				m_lstUndo.push_back(*pUndo.release());
			}

			return bRet;
		}

		bool AssetDestroy2(Asset::ID aid, const PeerID& pid)
		{
			auto it = m_Assets.find(aid);
			if (m_Assets.end() == it)
				return false;

			auto& x = it->second;
			if (x.m_Pid != pid)
				return false;

			if (x.m_Amount)
				return false;

			m_Assets.erase(it);
			return true;
		}

		ContractTestProcessor()
		{
			//m_Dbg.m_Stack = true;
			//m_Dbg.m_Instructions = true;
			//m_Dbg.m_ExtCall = true;
		}

		uint32_t m_Cycles;

		void CallFarN(const ContractID& cid, uint32_t iMethod, void* pArgs, uint32_t nArgs, uint8_t bInheritContext)
		{
			m_Stack.AliasAlloc(nArgs);
			memcpy(m_Stack.get_AliasPtr(), pArgs, nArgs);

			size_t nFrames = m_FarCalls.m_Stack.size();

			Wasm::Word nSp = m_Stack.get_AlasSp();
			CallFar(cid, iMethod, nSp);

			if (bInheritContext)
			{
				auto it = m_FarCalls.m_Stack.rbegin();
				auto& fr0 = *it;
				auto& fr1 = *(++it);
				fr0.m_Cid = fr1.m_Cid;
			}

			bool bWasm = false;
			for (; m_FarCalls.m_Stack.size() > nFrames; m_Cycles++)
			{
				bWasm = true;

				DischargeUnits(Limits::Cost::Cycle);
				RunOnce();

				if (m_Dbg.m_pOut)
				{
					std::cout << m_Dbg.m_pOut->str();
					m_Dbg.m_pOut->str("");

					if (m_Cycles >= 100000)
						m_Dbg.m_pOut = nullptr; // in debug max num of cycles takes too long because if this
				}
			}

			if (bWasm) {
				verify_test(nSp == m_Stack.get_AlasSp()); // stack must be restored
			}
			else {
				// in 'host' mode the stack will not be restored automatically, if ther was a call to StackAlloc
				verify_test(nSp >= m_Stack.get_AlasSp());
				m_Stack.set_AlasSp(nSp);
			}

			memcpy(pArgs, m_Stack.get_AliasPtr(), nArgs);
			m_Stack.AliasFree(nArgs);
		}

		void RunMany(const ContractID& cid, uint32_t iMethod, const Blob& args)
		{
			std::ostringstream os;
			//m_Dbg.m_pOut = &os;

			os << "BVM Method: " << cid << ":" << iMethod << std::endl;

			InitStackPlus(0);

			HeapReserveStrict(get_HeapLimit()); // this is necessary as long as we run shaders natively (not via wasm). Heap mem should not be reallocated

			m_Charge = Limits::BlockCharge; // default
			uint32_t nUnitsMax = m_Charge;

			Shaders::Env::g_pEnv = this;
			m_Cycles = 0;

			CallFarN(cid, iMethod, Cast::NotConst(args.p), args.n, 0);

			os << "Done in " << m_Cycles << " cycles, Discharge=" << (nUnitsMax - m_Charge) << std::endl << std::endl;
			std::cout << os.str();
		}

		bool RunGuarded(const ContractID& cid, uint32_t iMethod, const Blob& args, const Blob* pCode)
		{
			bool ret = true;
			size_t nChanges = m_lstUndo.size();

			if (!iMethod)
			{
				// c'tor
				assert(pCode);
				get_Cid(Cast::NotConst(cid), *pCode, args); // c'tor is empty
				SaveVar(cid, reinterpret_cast<const uint8_t*>(pCode->p), pCode->n);
			}

			try
			{
				RunMany(cid, iMethod, args);

				if (1 == iMethod) // d'tor
					SaveVar(cid, nullptr, 0);

			}
			catch (const std::exception& e) {
				std::cout << "*** Shader Execution failed. Undoing changes" << std::endl;
				std::cout << e.what() << std::endl;

				UndoChanges(nChanges);
				m_FarCalls.m_Stack.Clear();

				ret = false;
			}

			return ret;
		}

		template <typename T>
		struct Converter
			:public Blob
		{
			Converter(T& arg)
			{
				Shaders::Convert<true>(arg);
				p = &arg;
				n = static_cast<uint32_t>(sizeof(arg));
			}

			~Converter()
			{
				T& arg = Cast::NotConst(*reinterpret_cast<const T*>(p));
				Shaders::Convert<false>(arg);
			}
		};

		template <typename TArg>
		bool RunGuarded_T(const ContractID& cid, uint32_t iMethod, TArg& args)
		{
			Converter<TArg> cvt(args);
			return RunGuarded(cid, iMethod, cvt, nullptr);
		}

		template <typename T>
		bool ContractCreate_T(ContractID& cid, const Blob& code, T& args) {
			Converter<T> cvt(args);
			return RunGuarded(cid, 0, cvt, &code);
		}

		template <typename T>
		bool ContractDestroy_T(const ContractID& cid, T& args)
		{
			Converter<T> cvt(args);
			return RunGuarded(cid, 1, cvt, nullptr);
		}

		//struct Code
		//{
		//	ByteBuffer m_Vault;
		//	ByteBuffer m_Oracle;
		//	ByteBuffer m_Dummy;
		//	ByteBuffer m_Sidechain;
		//	ByteBuffer m_StableCoin;
		//	ByteBuffer m_Faucet;
		//	ByteBuffer m_Roulette;
		//	ByteBuffer m_Perpetual;
		//	ByteBuffer m_Pipe;
		//	ByteBuffer m_MirrorCoin;
		//	ByteBuffer m_Voting;
		//	ByteBuffer m_DemoXdao;

		//} m_Code;

		//ContractID m_cidVault;
		//ContractID m_cidOracle;
		//ContractID m_cidStableCoin;
		//ContractID m_cidFaucet;
		//ContractID m_cidRoulette;
		//ContractID m_cidDummy;
		//ContractID m_cidSidechain;
		//ContractID m_cidPerpetual;
		//ContractID m_cidPipe;
		//ContractID m_cidMirrorCoin1;
		//ContractID m_cidMirrorCoin2;
		//ContractID m_cidVoting;
		//ContractID m_cidDemoXdao;

		struct {

			Shaders::Eth::Header m_Header;
			uint32_t m_DatasetCount;
			ByteBuffer m_Proof;

		} m_Eth;

		static void AddCodeEx(ByteBuffer& res, const char* sz, Kind kind)
		{
			std::FStream fs;
			fs.Open(sz, true, true);

			res.resize(static_cast<size_t>(fs.get_Remaining()));
			if (!res.empty())
				fs.read(&res.front(), res.size());

			Processor::Compile(res, res, kind);
		}

		void AddCode(ByteBuffer& res, const char* sz)
		{
			AddCodeEx(res, sz, Kind::Contract);
		}

		template <typename T>
		T& CastArg(Wasm::Word nArg)
		{
			return Cast::NotConst(get_AddrAsR<T>(nArg));
		}

		struct TempFrame
		{
			ContractTestProcessor& m_This;
			FarCalls::Frame m_Frame;

			TempFrame(ContractTestProcessor& x, const ContractID& cid)
				:m_This(x)
			{
				m_Frame.m_Cid = cid;
				m_Frame.m_FarRetAddr = 0;
				m_This.m_FarCalls.m_Stack.push_back(m_Frame);
			}

			~TempFrame()
			{
				// don't call pop_back, in case of exc following interpreter frames won't be popped
				m_This.m_FarCalls.m_Stack.erase(intrusive::list<FarCalls::Frame>::s_iterator_to(m_Frame));
			}
		};

		//virtual void CallFar(const ContractID& cid, uint32_t iMethod, Wasm::Word pArgs) override
		//{
		//	if (cid == m_cidVault)
		//	{
		//		//TempFrame f(*this, cid);
		//		//switch (iMethod)
		//		//{
		//		//case 0: Shaders::Vault::Ctor(nullptr); return;
		//		//case 1: Shaders::Vault::Dtor(nullptr); return;
		//		//case 2: Shaders::Vault::Method_2(CastArg<Shaders::Vault::Deposit>(pArgs)); return;
		//		//case 3: Shaders::Vault::Method_3(CastArg<Shaders::Vault::Withdraw>(pArgs)); return;
		//		//}
		//	}

		//	if (cid == m_cidOracle)
		//	{
		//		//TempFrame f(*this, cid);
		//		//switch (iMethod)
		//		//{
		//		//case 0: Shaders::Oracle::Ctor(CastArg<Shaders::Oracle::Create<0> >(pArgs)); return;
		//		//case 1: Shaders::Oracle::Dtor(nullptr); return;
		//		//case 2: Shaders::Oracle::Method_2(CastArg<Shaders::Oracle::Set>(pArgs)); return;
		//		//case 3: Shaders::Oracle::Method_3(CastArg<Shaders::Oracle::Get>(pArgs)); return;
		//		//}
		//	}

		//	if (cid == m_cidStableCoin)
		//	{
		//		//TempFrame f(*this, cid);
		//		//switch (iMethod)
		//		//{
		//		//case 0: Shaders::StableCoin::Ctor(CastArg<Shaders::StableCoin::Create<0> >(pArgs)); return;
		//		//case 1: Shaders::StableCoin::Dtor(nullptr); return;
		//		//case 2: Shaders::StableCoin::Method_2(CastArg<Shaders::StableCoin::UpdatePosition>(pArgs)); return;
		//		//case 3: Shaders::StableCoin::Method_3(CastArg<Shaders::StableCoin::PlaceBid>(pArgs)); return;
		//		//case 4: Shaders::StableCoin::Method_4(CastArg<Shaders::StableCoin::Grab>(pArgs)); return;
		//		//}
		//	}

		//	if (cid == m_cidFaucet)
		//	{
		//		//TempFrame f(*this, cid);
		//		//switch (iMethod)
		//		//{
		//		//case 0: Shaders::Faucet::Ctor(CastArg<Shaders::Faucet::Params>(pArgs)); return;
		//		//case 1: Shaders::Faucet::Dtor(nullptr); return;
		//		//case 2: Shaders::Faucet::Method_2(CastArg<Shaders::Faucet::Deposit>(pArgs)); return;
		//		//case 3: Shaders::Faucet::Method_3(CastArg<Shaders::Faucet::Withdraw>(pArgs)); return;
		//		//}
		//	}

		//	if (cid == m_cidRoulette)
		//	{
		//		//TempFrame f(*this, cid);
		//		//switch (iMethod)
		//		//{
		//		//case 0: Shaders::Roulette::Ctor(CastArg<Shaders::Roulette::Params>(pArgs)); return;
		//		//case 1: Shaders::Roulette::Dtor(nullptr); return;
		//		//case 2: Shaders::Roulette::Method_2(CastArg<Shaders::Roulette::Spin>(pArgs)); return;
		//		//case 3: Shaders::Roulette::Method_3(nullptr); return;
		//		//case 4: Shaders::Roulette::Method_4(CastArg<Shaders::Roulette::Bid>(pArgs)); return;
		//		//case 5: Shaders::Roulette::Method_5(CastArg<Shaders::Roulette::Take>(pArgs)); return;
		//		//}
		//	}

		//	if (cid == m_cidDummy)
		//	{
		//		//TempFrame f(*this, cid);
		//		//switch (iMethod)
		//		//{
		//		//case 9: Shaders::Dummy::Method_9(CastArg<Shaders::Dummy::VerifyBeamHeader>(pArgs)); return;
		//		//case 11: Shaders::Dummy::Method_11(CastArg<Shaders::Dummy::TestRingSig>(pArgs)); return;
		//		//case 12: Shaders::Dummy::Method_12(CastArg<Shaders::Dummy::TestEthHeader>(pArgs)); return;
		//		//}
		//	}

		//	if (cid == m_cidSidechain)
		//	{
		//		//TempFrame f(*this, cid);
		//		//switch (iMethod)
		//		//{
		//		//case 0: Shaders::Sidechain::Ctor(CastArg<Shaders::Sidechain::Init>(pArgs)); return;
		//		//case 2: Shaders::Sidechain::Method_2(CastArg<Shaders::Sidechain::Grow<0> >(pArgs)); return;
		//		//case 3: Shaders::Sidechain::Method_3(CastArg<Shaders::Sidechain::VerifyProof<0> >(pArgs)); return;
		//		//case 4: Shaders::Sidechain::Method_4(CastArg<Shaders::Sidechain::WithdrawComission>(pArgs)); return;
		//		//}
		//	}

		//	if (cid == m_cidPerpetual)
		//	{
		//		//TempFrame f(*this, cid);
		//		//switch (iMethod)
		//		//{
		//		//case 0: Shaders::Perpetual::Ctor(CastArg<Shaders::Perpetual::Create>(pArgs)); return;
		//		//case 2: Shaders::Perpetual::Method_2(CastArg<Shaders::Perpetual::CreateOffer>(pArgs)); return;
		//		//case 3: Shaders::Perpetual::Method_3(CastArg<Shaders::Perpetual::CancelOffer>(pArgs)); return;
		//		//}
		//	}

		//	if (cid == m_cidPipe)
		//	{
		//		//TempFrame f(*this, cid);
		//		//switch (iMethod)
		//		//{
		//		//case 0: Shaders::Pipe::Ctor(CastArg<Shaders::Pipe::Create>(pArgs)); return;
		//		//case 2: Shaders::Pipe::Method_2(CastArg<Shaders::Pipe::SetRemote>(pArgs)); return;
		//		//case 3: Shaders::Pipe::Method_3(CastArg<Shaders::Pipe::PushLocal0>(pArgs)); return;
		//		//case 4: Shaders::Pipe::Method_4(CastArg<Shaders::Pipe::PushRemote0>(pArgs)); return;
		//		//case 5: Shaders::Pipe::Method_5(CastArg<Shaders::Pipe::FinalyzeRemote>(pArgs)); return;
		//		//case 6: Shaders::Pipe::Method_6(CastArg<Shaders::Pipe::ReadRemote0>(pArgs)); return;
		//		//case 7: Shaders::Pipe::Method_7(CastArg<Shaders::Pipe::Withdraw>(pArgs)); return;
		//		//}
		//	}

		//	if ((cid == m_cidMirrorCoin1) || (cid == m_cidMirrorCoin2))
		//	{
		//		//TempFrame f(*this, cid);
		//		//switch (iMethod)
		//		//{
		//		//case 0: Shaders::MirrorCoin::Ctor(CastArg<Shaders::MirrorCoin::Create0>(pArgs)); return;
		//		//case 2: Shaders::MirrorCoin::Method_2(CastArg<Shaders::MirrorCoin::SetRemote>(pArgs)); return;
		//		//case 3: Shaders::MirrorCoin::Method_3(CastArg<Shaders::MirrorCoin::Send>(pArgs)); return;
		//		//case 4: Shaders::MirrorCoin::Method_4(CastArg<Shaders::MirrorCoin::Receive>(pArgs)); return;
		//		//}
		//	}

		//	if (cid == m_cidVoting)
		//	{
		//		//TempFrame f(*this, cid);
		//		//switch (iMethod)
		//		//{
		//		//case 2: Shaders::Voting::Method_2(CastArg<Shaders::Voting::OpenProposal>(pArgs)); return;
		//		//case 3: Shaders::Voting::Method_3(CastArg<Shaders::Voting::Vote>(pArgs)); return;
		//		//case 4: Shaders::Voting::Method_4(CastArg<Shaders::Voting::Withdraw>(pArgs)); return;
		//		//}
		//	}

		//	if (cid == m_cidDemoXdao)
		//	{
		//		//TempFrame f(*this, cid);
		//		//switch (iMethod)
		//		//{
		//		//case 0: Shaders::DemoXdao::Ctor(nullptr); return;
		//		//case 3: Shaders::DemoXdao::Method_3(CastArg<Shaders::DemoXdao::GetPreallocated>(pArgs)); return;
		//		//case 4: Shaders::DemoXdao::Method_4(CastArg<Shaders::DemoXdao::UpdPosFarming>(pArgs)); return;
		//		//}
		//	}

		//	ProcessorContract::CallFar(cid, iMethod, pArgs);
		//}

	};
}