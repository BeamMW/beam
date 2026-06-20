#include "../common.h"
#include "../Explorer/parser_module_abi.h"
#include "contract.h"

static const ShaderID s_pSid[] = { Minter::s_SID };

static void OnKind() { Env::DocAddText("kind", "Minter"); }

static void WriteMinterSettings(const Minter::Settings& stg)
{
	DocAddCid("Dao-Vault", stg.m_cidDaoVault);
	DocAddAmount("Issuance fee", stg.m_IssueFee);
}

static void OnMethod_Inner(uint32_t iMethod, const void* pArg, uint32_t nArg)
{
	switch (iMethod)
	{
	case Minter::Method::Init::s_iMethod:
		Env::DocAddText("method", "Create");
		if (nArg >= sizeof(Minter::Method::Init))
		{
			auto* p = (const Minter::Method::Init*) pArg;
			Env::DocGroup gr("params");
			WriteMinterSettings(p->m_Settings);
		}
		break;

	case 1: Env::DocAddText("method", "Destroy"); break;

	case Minter::Method::View::s_iMethod:
		if (nArg >= sizeof(Minter::Method::View))
		{
			auto* p = (const Minter::Method::View*) pArg;
			Env::DocAddText("method", "View");
			Env::DocGroup gr("params");
			DocAddAid("aid", p->m_Aid);
		}
		break;

	case Minter::Method::CreateToken::s_iMethod:
		if (nArg >= sizeof(Minter::Method::CreateToken))
		{
			auto* p = (const Minter::Method::CreateToken*) pArg;
			Env::DocAddText("method", "Create token");
			Env::DocGroup gr("params");
			DocAddAmountBig("Limit", p->m_Limit.m_Lo, p->m_Limit.m_Hi);
			if (Minter::PubKeyFlag::s_Cid == p->m_pkOwner.m_Y)
				DocAddCid("Owner", p->m_pkOwner.m_X);
			else
				DocAddPk("Owner", p->m_pkOwner);
		}
		break;

	case Minter::Method::Withdraw::s_iMethod:
		if (nArg >= sizeof(Minter::Method::Withdraw))
		{
			auto* p = (const Minter::Method::Withdraw*) pArg;
			Env::DocAddText("method", "Withdraw");
			Env::DocGroup gr("params");
			DocAddAmount("amount", p->m_Value);
			DocAddAid("aid", p->m_Aid);
		}
		break;
	}
}

static void OnState_Inner(const ContractID& cid)
{
	Env::Key_T<uint8_t> k;
	_POD_(k.m_Prefix.m_Cid) = cid;
	k.m_KeyInContract = Minter::Tags::s_Settings;

	Minter::Settings s;
	if (!Env::VarReader::Read_T(k, s))
		return;

	{
		Env::DocGroup gr2("Settings");
		WriteMinterSettings(s);
	}

	{
		Env::DocGroup gr2("Tokens");
		DocSetType("table");
		Env::DocArray gr3("value");

		{
			Env::DocArray gr4("");
			DocAddTableHeader("Aid");
			DocAddTableHeader("Metadata");
			DocAddTableHeader("Owner");
			DocAddTableHeader("Minted");
			DocAddTableHeader("Limit");
		}

		Env::Key_T<Minter::Token::Key> k0, k1;
		_POD_(k0.m_Prefix.m_Cid) = cid;
		_POD_(k1.m_Prefix.m_Cid) = cid;
		k0.m_KeyInContract.m_Aid = 0;
		k1.m_KeyInContract.m_Aid = (AssetID) -1;

		for (Env::VarReader r(k0, k1); ; )
		{
			Minter::Token mt;
			if (!r.MoveNext_T(k0, mt))
				break;

			Env::DocArray gr4("");

			DocAddAid("", k0.m_KeyInContract.m_Aid);

			char szMetadata[1024 * 16 + 1];
			AssetInfo ai;
			auto nMetadata = Env::get_AssetInfo(k0.m_KeyInContract.m_Aid, ai, szMetadata, sizeof(szMetadata) - 1);
			szMetadata[nMetadata] = 0;

			Env::DocAddText("", szMetadata);

			if (Minter::PubKeyFlag::s_Cid & mt.m_pkOwner.m_Y)
				DocAddCid("", mt.m_pkOwner.m_X);
			else
				DocAddMonoblob("", mt.m_pkOwner);

			DocAddAmountBig("", mt.m_Minted.m_Lo, mt.m_Minted.m_Hi);
			DocAddAmountBig("", mt.m_Limit.m_Lo, mt.m_Limit.m_Hi);
		}
	}
}

BEAM_EXPORT uint32_t Method_3(ShaderID* out_buf, uint32_t out_cap)
{
	return ParserModule_FillSids(out_buf, out_cap, s_pSid, _countof(s_pSid));
}

BEAM_EXPORT void Method_0(const ShaderID& /*sid*/, const ContractID& /*cid*/, uint32_t iMethod, const void* pArg, uint32_t nArg)
{
	Env::DocGroup gr("");
	OnKind();
	OnMethod_Inner(iMethod, pArg, nArg);
}

BEAM_EXPORT void Method_1(const ShaderID& /*sid*/, const ContractID& /*cid*/)
{
	Env::DocGroup gr("");
	OnKind();
}

BEAM_EXPORT void Method_2(const ShaderID& /*sid*/, const ContractID& cid)
{
	Env::DocGroup gr("");
	OnKind();
	{
		Env::DocGroup grSt("State");
		OnState_Inner(cid);
	}
}
