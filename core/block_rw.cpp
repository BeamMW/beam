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
#include "block_rw.h"
#include "utility/serialize.h"
#include "utilstrencodings.h"
#include "core/serialization_adapters.h"
#include "aes.h"
#include "pkcs5_pbkdf2.h"
#include "radixtree.h"

namespace beam
{
	size_t TxBase::IReader::get_SizeNetto()
	{
		SerializerSizeCounter ssc;
		Reset();

		for (; m_pUtxoIn; NextUtxoIn())
			ssc & *m_pUtxoIn;
		for (; m_pUtxoOut; NextUtxoOut())
			ssc & *m_pUtxoOut;
		for (; m_pKernel; NextKernel())
			ssc & *m_pKernel;

		return ssc.m_Counter.m_Value;
	}

	void TxBase::IWriter::Dump(IReader&& r)
	{
		r.Reset();

		for (; r.m_pUtxoIn; r.NextUtxoIn())
			Write(*r.m_pUtxoIn);
		for (; r.m_pUtxoOut; r.NextUtxoOut())
			Write(*r.m_pUtxoOut);
		for (; r.m_pKernel; r.NextKernel())
			Write(*r.m_pKernel);
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
			int iInp = 0, iOut = 0; // initialized just to suppress the warning, not really needed

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
					int n = CmpInOut(*pInp, *pOut);
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
				Write(*pInp);
				ppR[iInp]->NextUtxoIn();
			}
			else
			{
				Write(*pOut);
				ppR[iOut]->NextUtxoOut();
			}
		}


		// Kernels
		while (true)
		{
			if (bStop)
				return false;

			const TxKernel* pKrn = NULL;
			int iSrc = 0; // initialized just to suppress the warning, not really needed

			for (int i = 0; i < nR; i++)
			{
				const TxKernel* po = ppR[i]->m_pKernel;
				if (po && (!pKrn || (*pKrn > *po)))
				{
					pKrn = po;
					iSrc = i;
				}
			}

			if (!pKrn)
				break;

			Write(*pKrn);
			ppR[iSrc]->NextKernel();
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

	/////////////
	// KeyString
	void KeyString::Export(const ECC::HKdf& v)
	{
		ECC::NoLeak<ECC::HKdf::Packed> p;
		v.Export(p.V);
		Export(&p.V, sizeof(p.V), 's');
	}

	void KeyString::Export(const ECC::HKdfPub& v)
	{
		ECC::NoLeak<ECC::HKdfPub::Packed> p;
		v.Export(p.V);
		Export(&p.V, sizeof(p.V), 'P');
	}

	void KeyString::Export(void* p, uint32_t nData, uint8_t nCode)
	{
		ByteBuffer bb;
		bb.resize(sizeof(MacValue) + nData + 1 + m_sMeta.size());
		MacValue& mv = reinterpret_cast<MacValue&>(bb.at(0));

		bb[sizeof(MacValue)] = nCode;
		memcpy(&bb.at(1) + sizeof(MacValue), p, nData);
		memcpy(&bb.at(1) + sizeof(MacValue) + nData, m_sMeta.c_str(), m_sMeta.size());

		XCrypt(mv, static_cast<uint32_t>(nData + 1 + m_sMeta.size()), true);

		m_sRes = EncodeBase64(&bb.at(0), bb.size());
	}

	bool KeyString::Import(ECC::HKdf& v)
	{
		ECC::NoLeak<ECC::HKdf::Packed> p;
		return
			Import(&p.V, sizeof(p.V), 's') &&
			v.Import(p.V);
	}

	bool KeyString::Import(ECC::HKdfPub& v)
	{
		ECC::NoLeak<ECC::HKdfPub::Packed> p;
		return
			Import(&p.V, sizeof(p.V), 'P') &&
			v.Import(p.V);
	}

	bool KeyString::Import(void* p, uint32_t nData, uint8_t nCode)
	{
		bool bInvalid = false;
		ByteBuffer bb = DecodeBase64(m_sRes.c_str(), &bInvalid);

		if (bInvalid || (bb.size() < sizeof(MacValue) + 1 + nData))
			return false;

		MacValue& mv = reinterpret_cast<MacValue&>(bb.at(0));
		MacValue mvOrg = mv;

		XCrypt(mv, static_cast<uint32_t>(bb.size()) - sizeof(mv), false);

		if ((mv != mvOrg) || (bb[sizeof(MacValue)] != nCode))
			return false;

		memcpy(p, &bb.at(1) + sizeof(MacValue), nData);

		m_sMeta.resize(bb.size() - (sizeof(MacValue) + 1 + nData));
		if (!m_sMeta.empty())
			memcpy(&m_sMeta.front(), &bb.at(1) + sizeof(MacValue) + nData, m_sMeta.size());

		return true;
	}

	void KeyString::XCrypt(MacValue& mv, uint32_t nSize, bool bEnc) const
	{
		static_assert(AES::s_KeyBytes == sizeof(m_hvSecret.V), "");
		AES::Encoder enc;
		enc.Init(m_hvSecret.V.m_pData);

		AES::StreamCipher c;
		ECC::NoLeak<ECC::Hash::Value> hvIV;
		ECC::Hash::Processor() << m_hvSecret.V >> hvIV.V;

		c.m_Counter = hvIV.V; // truncated
		c.m_nBuf = 0;

		ECC::Hash::Mac hmac;
		hmac.Reset(m_hvSecret.V.m_pData, m_hvSecret.V.nBytes);

		if (bEnc)
			hmac.Write(reinterpret_cast<uint8_t*>(&mv + 1), nSize);

		c.XCrypt(enc, reinterpret_cast<uint8_t*>(&mv + 1), nSize);

		if (!bEnc)
			hmac.Write(reinterpret_cast<uint8_t*>(&mv + 1), nSize);

		hmac >> hvIV.V;
		mv = hvIV.V;
	}

	void KeyString::SetPassword(const std::string& s)
	{
		SetPassword(Blob(s.data(), static_cast<uint32_t>(s.size())));
	}

	void KeyString::SetPassword(const Blob& b)
	{
		int nRes = pkcs5_pbkdf2(
			reinterpret_cast<const uint8_t*>(b.p),
			b.n,
			NULL,
			0,
			m_hvSecret.V.m_pData,
			m_hvSecret.V.nBytes,
			65536);

		if (nRes)
			throw std::runtime_error("pbkdf2 fail");
	}

	/////////////
	// RecoveryInfo
	void RecoveryInfo::Reader::ThrowRulesMismatch()
	{
		throw std::runtime_error("Rules mismatch");
	}

	void RecoveryInfo::Writer::Open(const char* sz, const Block::ChainWorkProof& cwp)
	{
		m_Stream.Open(sz, false, true);
		yas::binary_oarchive<std::FStream, SERIALIZE_OPTIONS> ser(m_Stream);

		Height hMax = cwp.m_Heading.m_Prefix.m_Height + cwp.m_Heading.m_vElements.size() - 1;

		const Rules& r = Rules::get();

		uint32_t nForks = 0;
		for (; nForks < _countof(r.pForks); nForks++)
		{
			if (hMax < r.pForks[nForks].m_Height)
				break;
		}

		ser & nForks;
		for (uint32_t iFork = 0; iFork < nForks; iFork++)
			ser & r.pForks[iFork].m_Hash;

		ser & cwp;
	}

	void RecoveryInfo::Reader::Open(const char* sz)
	{
		m_Stream.Open(sz, true, true);
		yas::binary_iarchive<std::FStream, SERIALIZE_OPTIONS> der(m_Stream);

		uint32_t nForks = 0;
		der & nForks;

		const Rules& r = Rules::get();
		if (nForks > _countof(r.pForks))
			ThrowRulesMismatch();

		for (uint32_t iFork = 0; iFork < nForks; iFork++)
		{
			ECC::Hash::Value hv;
			der & hv;

			if (hv != r.pForks[iFork].m_Hash)
				ThrowRulesMismatch();
		}

		der & m_Cwp;

		if (!m_Cwp.IsValid(&m_Tip))
			throw std::runtime_error("CWP error");

		if ((nForks < _countof(r.pForks)) && (m_Tip.m_Height >= r.pForks[nForks].m_Height))
			ThrowRulesMismatch();
	}

	void RecoveryInfo::Writer::Write(const Entry& x)
	{
		yas::binary_oarchive<std::FStream, SERIALIZE_OPTIONS> ser(m_Stream);
		ser & x;
	}

	bool RecoveryInfo::Reader::Read(Entry& x)
	{
		if (!m_Stream.get_Remaining())
			return false;

		yas::binary_iarchive<std::FStream, SERIALIZE_OPTIONS> der(m_Stream);
		der & x;

		UtxoTree::Key::Data d;
		d.m_Commitment = x.m_Output.m_Commitment;
		d.m_Maturity = x.m_Output.get_MinMaturity(x.m_CreateHeight);

		UtxoTree::Key key;
		key = d;

		if (!m_UtxoTree.Add(key))
			throw std::runtime_error("UTXO order mismatch");

		return true;
	}

	void RecoveryInfo::Reader::Finalyze()
	{
		Merkle::Hash hv;
		m_UtxoTree.Flush(hv);

		if (!(m_Cwp.m_hvRootLive == hv))
			throw std::runtime_error("UTXO hash mismatch");
	}

} // namespace beam
