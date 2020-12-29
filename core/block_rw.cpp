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
#include "serialization_adapters.h"
#include "shielded.h"
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
			// a little bit inaccurate, assuming worst-case
			yas::detail::SaveKrn(ssc, *m_pKernel, false);

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
	void KeyString::ExportS(const Key::IKdf& v)
	{
		ECC::NoLeak<ECC::HKdf::Packed> p;
		assert(v.ExportS(nullptr) == sizeof(p));
		v.ExportS(&p);
		Export(&p.V, sizeof(p.V), 's');
	}

	void KeyString::ExportP(const Key::IPKdf& v)
	{
		ECC::NoLeak<ECC::HKdfPub::Packed> p;
		assert(v.ExportP(nullptr) == sizeof(p));
		v.ExportP(&p);
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

	struct RecoveryInfo::IParser::Context
	{
		IParser& m_Parser;
		std::FStream m_Stream;
		yas::binary_iarchive<std::FStream, SERIALIZE_OPTIONS> m_Der;
		Block::ChainWorkProof m_Cwp;
		Block::SystemState::Full m_Tip;
		uint64_t m_Total;

		UtxoTree::Compact m_UtxoTree;
		Merkle::CompactMmr m_Shielded;
		Merkle::CompactMmr m_Assets;

		Context(IParser& p)
			:m_Parser(p)
			,m_Der(m_Stream)
		{
		}

		void Open(const char*);
		bool Proceed();
		bool ProceedUtxos();
		bool ProceedShielded();
		bool ProceedAssets();
		void Finalyze();

		bool OnProgress() {
			return m_Parser.OnProgress(m_Total - m_Stream.get_Remaining(), m_Total);
		}

		static void ThrowRulesMismatch() {
			throw std::runtime_error("Rules mismatch");
		}

		static void ThrowBadData() {
			throw std::runtime_error("Data inconsistent");
		}
	};

	void RecoveryInfo::RecoveryInfo::IParser::Context::Open(const char* sz)
	{
		m_Stream.Open(sz, true, true);
		m_Total = m_Stream.get_Remaining();

		uint32_t nForks = 0;
		m_Der & nForks;

		const Rules& r = Rules::get();
		if (nForks > _countof(r.pForks))
			ThrowRulesMismatch();

		for (uint32_t iFork = 0; iFork < nForks; iFork++)
		{
			ECC::Hash::Value hv;
			m_Der & hv;

			if (hv != r.pForks[iFork].m_Hash)
				ThrowRulesMismatch();
		}

		m_Der & m_Cwp;

		if (!m_Cwp.IsValid(&m_Tip))
			ThrowBadData();

		if ((nForks < _countof(r.pForks)) && (m_Tip.m_Height >= r.pForks[nForks].m_Height))
			ThrowRulesMismatch();
	}

	void RecoveryInfo::RecoveryInfo::IParser::Context::Finalyze()
	{
		struct Verifier
			:public Block::SystemState::Evaluator
		{
			Context& m_This;
			Verifier(Context& ctx) :m_This(ctx) {}

			virtual bool get_Utxos(Merkle::Hash& hv) override
			{
				m_This.m_UtxoTree.Flush(hv);
				return true;
			}

			virtual bool get_Shielded(Merkle::Hash& hv) override
			{
				m_This.m_Shielded.get_Hash(hv);
				return true;
			}

			virtual bool get_Assets(Merkle::Hash& hv) override
			{
				m_This.m_Assets.get_Hash(hv);
				return true;
			}
		};

		Verifier v(*this);
		v.m_Height = m_Tip.m_Height;

		Merkle::Hash hv;
		BEAM_VERIFY(v.get_Live(hv));

		if (!(m_Cwp.m_hvRootLive == hv))
			ThrowBadData();
	}

	bool RecoveryInfo::IParser::Proceed(const char* sz)
	{
		Context ctx(*this);
		ctx.Open(sz);
		return ctx.Proceed();
	}

	bool RecoveryInfo::IParser::Context::Proceed()
	{
		std::vector<Block::SystemState::Full> vec;
		m_Cwp.UnpackStates(vec);
		if (!m_Parser.OnStates(vec))
			return false;

		if (!ProceedUtxos())
			return false;

		const Rules& r = Rules::get();
		if (m_Tip.m_Height >= r.pForks[2].m_Height)
		{
			if (!ProceedShielded())
				return false;

			if (!ProceedAssets())
				return false;
		}

		Finalyze();
		return true;
	}

	bool RecoveryInfo::IParser::Context::ProceedUtxos()
	{
		while (true)
		{
			if (!m_Stream.get_Remaining())
				break; // old-style terminator

			Height h;
			m_Der & h;

			if (MaxHeight == h)
				break;

			Output outp;
			m_Der & outp;

			UtxoTree::Key::Data d;
			d.m_Commitment = outp.m_Commitment;
			d.m_Maturity = outp.get_MinMaturity(h);

			UtxoTree::Key key;
			key = d;

			if (!m_UtxoTree.Add(key))
				ThrowBadData();

			if (!m_Parser.OnUtxo(h, outp))
				return false;

			if (!OnProgress())
				return false;
		}

		return true;
	}

	bool RecoveryInfo::IParser::Context::ProceedShielded()
	{
		TxoID nOuts = 0;

		while (true)
		{
			Height h;
			m_Der & h;

			if (MaxHeight == h)
				break;

			uint8_t nFlags = 0;
			m_Der & nFlags;

			Merkle::Hash hv;

			if (Flags::Output & nFlags)
			{
				ShieldedTxo txo;
				m_Der & txo;
				m_Der & hv;

				assert(!txo.m_pAsset); // the asset proof itself is omitted.
				if (Flags::HadAsset & nFlags)
					txo.m_pAsset.reset(new Asset::Proof);

				ShieldedTxo::DescriptionOutp dOutp;
				dOutp.m_Commitment = txo.m_Commitment;
				dOutp.m_SerialPub = txo.m_Ticket.m_SerialPub;
				dOutp.m_ID = nOuts++;
				dOutp.m_Height = h;

				if (!m_Parser.OnShieldedOut(dOutp, txo, hv))
					return false;

				dOutp.get_Hash(hv);
			}
			else
			{
				ShieldedTxo::DescriptionInp dInp;
				m_Der & dInp.m_SpendPk;
				dInp.m_Height = h;

				if (!m_Parser.OnShieldedIn(dInp))
					return false;

				dInp.get_Hash(hv);
			}

			m_Shielded.Append(hv);

			if (!OnProgress())
				return false;
		}

		return true;
	}

	bool RecoveryInfo::IParser::Context::ProceedAssets()
	{
		while (true)
		{
			Asset::Full ai;
			m_Der & ai.m_ID;

			if (ai.m_ID > Asset::s_MaxCount)
				break;

			m_Der & Cast::Down<Asset::Info>(ai);

			Merkle::Hash hv;
			ai.get_Hash(hv);
			m_Assets.Append(hv);

			if (!m_Parser.OnAsset(ai))
				return false;

			if (!OnProgress())
				return false;
		}
		
		return true;
	}

	void RecoveryInfo::IRecognizer::Init(const Key::IPKdf::Ptr& pKdf, Key::Index nMaxShieldedIdx /* = 1 */)
	{
		m_pOwner = pKdf;
		if (pKdf)
		{
			m_vSh.resize(nMaxShieldedIdx);
			
			for (Key::Index nIdx = 0; nIdx < nMaxShieldedIdx; nIdx++)
				m_vSh[nIdx].FromOwner(*pKdf, nIdx);
		}
	}

	bool RecoveryInfo::IRecognizer::OnUtxo(Height h, const Output& outp)
	{
		if (m_pOwner)
		{
			CoinID cid;
			if (outp.Recover(h, *m_pOwner, cid))
				return OnUtxoRecognized(h, outp, cid);
		}

		return true;
	}

	bool RecoveryInfo::IRecognizer::OnShieldedOut(const ShieldedTxo::DescriptionOutp& dout, const ShieldedTxo& txo, const ECC::Hash::Value& hvMsg)
	{
		for (Key::Index nIdx = 0; nIdx < static_cast<Key::Index>(m_vSh.size()); nIdx++)
		{
			ShieldedTxo::DataParams pars;

			if (pars.m_Ticket.Recover(txo.m_Ticket, m_vSh[nIdx]))
			{
				ECC::Oracle oracle;
				oracle << hvMsg;

				if (pars.m_Output.Recover(txo, pars.m_Ticket.m_SharedSecret, oracle))
					return OnShieldedOutRecognized(dout, pars, nIdx);
			}
		}

		return true;
	}

	bool RecoveryInfo::IRecognizer::OnAsset(Asset::Full& ai)
	{
		if (m_pOwner && ai.Recognize(*m_pOwner))
			return OnAssetRecognized(ai);

		return true;
	}

} // namespace beam
