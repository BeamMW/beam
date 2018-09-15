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

//#include <ctime>
#include "block_crypt.h"
#include "../utility/serialize.h"
#include "../core/serialization_adapters.h"

namespace beam
{
	/////////////
	// RW
	const char* const Block::BodyBase::RW::s_pszSufix[s_Datas] = {
		"ui",
		"uo",
		"ki",
		"ko",
		"hd",
	};

	void Block::BodyBase::RW::GetPath(std::string& s, int iData) const
	{
		assert(iData < s_Datas);
		s = m_sPath + s_pszSufix[iData];
	}

	void Block::BodyBase::RW::Open(bool bRead)
	{
		using namespace std;

		m_bRead = bRead;

		if (bRead)
			for (int i = 0; i < _countof(m_pS); i++)
				OpenInternal(i);
	}

	bool Block::BodyBase::RW::OpenInternal(int iData)
	{
		std::string s;
		GetPath(s, iData);
		return m_pS[iData].Open(s.c_str(), m_bRead);
	}

	void Block::BodyBase::RW::Delete()
	{
		for (int i = 0; i < _countof(m_pS); i++)
		{
			std::string s;
			GetPath(s, i);
			DeleteFile(s.c_str());
		}
	}

	void Block::BodyBase::RW::Close()
	{
		for (size_t i = 0; i < _countof(m_pS); i++)
			m_pS[i].Close();
	}

	Block::BodyBase::RW::~RW()
	{
		if (m_bAutoDelete)
		{
			Close();
			Delete();
		}
	}

	void Block::BodyBase::RW::Reset()
	{
		for (size_t i = 0; i < _countof(m_pS); i++)
			if (m_pS[i].IsOpen())
				m_pS[i].Restart();

		// preload
		LoadInternal(m_pUtxoIn, 0, m_pGuardUtxoIn);
		LoadInternal(m_pUtxoOut, 1, m_pGuardUtxoOut);
		LoadInternal(m_pKernelIn, 2, m_pGuardKernelIn);
		LoadInternal(m_pKernelOut, 3, m_pGuardKernelOut);
	}

	void Block::BodyBase::RW::Flush()
	{
		for (size_t i = 0; i < _countof(m_pS); i++)
			if (m_pS[i].IsOpen())
				m_pS[i].Flush();
	}

	void Block::BodyBase::RW::Clone(Ptr& pOut)
	{
		RW* pRet = new RW;
		pOut.reset(pRet);

		pRet->m_sPath = m_sPath;
		pRet->Open(m_bRead);
	}

	void Block::BodyBase::RW::NextUtxoIn()
	{
		LoadInternal(m_pUtxoIn, 0, m_pGuardUtxoIn);
	}

	void Block::BodyBase::RW::NextUtxoOut()
	{
		LoadInternal(m_pUtxoOut, 1, m_pGuardUtxoOut);
	}

	void Block::BodyBase::RW::NextKernelIn()
	{
		LoadInternal(m_pKernelIn, 2, m_pGuardKernelIn);
	}

	void Block::BodyBase::RW::NextKernelOut()
	{
		LoadInternal(m_pKernelOut, 3, m_pGuardKernelOut);
	}

	void Block::BodyBase::RW::get_Start(BodyBase& body, SystemState::Sequence::Prefix& prefix)
	{
		if (!m_pS[4].IsOpen())
			std::ThrowIoError();
		yas::binary_iarchive<std::FStream, SERIALIZE_OPTIONS> arc(m_pS[4]);

		ECC::Hash::Value hv;
		arc & hv;

		if (hv != Rules::get().Checksum)
			throw std::runtime_error("Block rules mismatch");

		arc & body;
		arc & prefix;
	}

	bool Block::BodyBase::RW::get_NextHdr(SystemState::Sequence::Element& elem)
	{
		std::FStream& s = m_pS[4];
		if (!s.get_Remaining())
			return false;

		yas::binary_iarchive<std::FStream, SERIALIZE_OPTIONS> arc(s);
		arc & elem;

		return true;
	}

	void Block::BodyBase::RW::WriteIn(const Input& v)
	{
		WriteInternal(v, 0);
	}

	void Block::BodyBase::RW::WriteIn(const TxKernel& v)
	{
		WriteInternal(v, 2);
	}

	void Block::BodyBase::RW::WriteOut(const Output& v)
	{
		WriteInternal(v, 1);
	}

	void Block::BodyBase::RW::WriteOut(const TxKernel& v)
	{
		WriteInternal(v, 3);
	}

	void Block::BodyBase::RW::put_Start(const BodyBase& body, const SystemState::Sequence::Prefix& prefix)
	{
		WriteInternal(Rules::get().Checksum, 4);
		WriteInternal(body, 4);
		WriteInternal(prefix, 4);
	}

	void Block::BodyBase::RW::put_NextHdr(const SystemState::Sequence::Element& elem)
	{
		WriteInternal(elem, 4);
	}

	template <typename T>
	void Block::BodyBase::RW::LoadInternal(const T*& pPtr, int iData, typename T::Ptr* ppGuard)
	{
		std::FStream& s = m_pS[iData];

		if (s.IsOpen() && s.get_Remaining())
		{
			ppGuard[0].swap(ppGuard[1]);
			//if (!ppGuard[0])
				ppGuard[0].reset(new T);

			yas::binary_iarchive<std::FStream, SERIALIZE_OPTIONS> arc(s);
			arc & *ppGuard[0];

			pPtr = ppGuard[0].get();
		}
		else
			pPtr = NULL;
	}

	template <typename T>
	void Block::BodyBase::RW::WriteInternal(const T& v, int iData)
	{
		std::FStream& s = m_pS[iData];
		if (!s.IsOpen() && !OpenInternal(iData))
			std::ThrowIoError();

		yas::binary_oarchive<std::FStream, SERIALIZE_OPTIONS> arc(s);
		arc & v;
	}

	void TxBase::IWriter::Dump(IReader&& r)
	{
		r.Reset();

		for (; r.m_pUtxoIn; r.NextUtxoIn())
			WriteIn(*r.m_pUtxoIn);
		for (; r.m_pUtxoOut; r.NextUtxoOut())
			WriteOut(*r.m_pUtxoOut);
		for (; r.m_pKernelIn; r.NextKernelIn())
			WriteIn(*r.m_pKernelIn);
		for (; r.m_pKernelOut; r.NextKernelOut())
			WriteOut(*r.m_pKernelOut);
	}

	bool TxBase::IWriter::Combine(IReader&& r0, IReader&& r1, const volatile bool& bStop)
	{
		IReader* ppR[] = { &r0, &r1 };
		return Combine(ppR, _countof(ppR), bStop);
	}

	bool TxBase::IWriter::Combine(IReader** ppR, int nR, const volatile bool& bStop)
	{
		for (int i = 0; i < nR; i++)
			ppR[i]->Reset();

		// Utxo
		while (true)
		{
			if (bStop)
				return false;

			const Input* pInp = NULL;
			const Output* pOut = NULL;
			int iInp, iOut;

			for (int i = 0; i < nR; i++)
			{
				const Input* pi = ppR[i]->m_pUtxoIn;
				if (pi && (!pInp || (*pInp > *pi)))
				{
					pInp = pi;
					iInp = i;
				}

				const Output* po = ppR[i]->m_pUtxoOut;
				if (po && (!pOut || (*pOut > *po)))
				{
					pOut = po;
					iOut = i;
				}
			}

			if (pInp)
			{
				if (pOut)
				{
					int n = pInp->cmp_CaM(*pOut);
					if (n > 0)
						pInp = NULL;
					else
						if (!n)
						{
							// skip both
							ppR[iInp]->NextUtxoIn();
							ppR[iOut]->NextUtxoOut();
							continue;
						}
				}
			}
			else
				if (!pOut)
					break;


			if (pInp)
			{
				WriteIn(*pInp);
				ppR[iInp]->NextUtxoIn();
			}
			else
			{
				WriteOut(*pOut);
				ppR[iOut]->NextUtxoOut();
			}
		}


		// Kernels
		while (true)
		{
			if (bStop)
				return false;

			const TxKernel* pInp = NULL;
			const TxKernel* pOut = NULL;
			int iInp, iOut;

			for (int i = 0; i < nR; i++)
			{
				const TxKernel* pi = ppR[i]->m_pKernelIn;
				if (pi && (!pInp || (*pInp > *pi)))
				{
					pInp = pi;
					iInp = i;
				}

				const TxKernel* po = ppR[i]->m_pKernelOut;
				if (po && (!pOut || (*pOut > *po)))
				{
					pOut = po;
					iOut = i;
				}
			}

			if (pInp)
			{
				if (pOut)
				{
					int n = pInp->cmp(*pOut);
					if (n > 0)
						pInp = NULL;
					else
						if (!n)
						{
							// skip both
							ppR[iInp]->NextUtxoIn();
							ppR[iOut]->NextUtxoOut();
							continue;
						}
				}
			}
			else
				if (!pOut)
					break;


			if (pInp)
			{
				WriteIn(*pInp);
				ppR[iInp]->NextKernelIn();
			}
			else
			{
				WriteOut(*pOut);
				ppR[iOut]->NextKernelOut();
			}
		}

		return true;
	}

	bool Block::BodyBase::IMacroWriter::CombineHdr(IMacroReader&& r0, IMacroReader&& r1, const volatile bool& bStop)
	{
		Block::BodyBase body0, body1;
		Block::SystemState::Sequence::Prefix prefix0, prefix1;
		Block::SystemState::Sequence::Element elem;

		r0.Reset();
		r0.get_Start(body0, prefix0);
		r1.Reset();
		r1.get_Start(body1, prefix1);

		body0.Merge(body1);
		put_Start(body0, prefix0);

		while (r0.get_NextHdr(elem))
		{
			if (bStop)
				return false;
			put_NextHdr(elem);
		}

		while (r1.get_NextHdr(elem))
		{
			if (bStop)
				return false;
			put_NextHdr(elem);
		}

		return true;
	}

} // namespace beam
