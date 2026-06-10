// Parser module for Gallery (versions 0, 1, 2 — same method shape).
#include "../common.h"
#include "../Explorer/parser_module_abi.h"
#include "contract.h"

static void OnKind(const ShaderID& sid)
{
	for (uint32_t i = 0; i < _countof(Gallery::s_pSID); i++)
	{
		if (_POD_(sid) == Gallery::s_pSID[i])
		{
			char szBuf[16] = "Gallery v";
			Utils::String::Decimal::Print(szBuf + 9, i);
			Env::DocAddText("kind", szBuf);
			return;
		}
	}
	Env::DocAddText("kind", "Gallery");
}

static void WriteGalleryAdrID(Gallery::Masterpiece::ID id) { Env::DocAddNum("art_id", Utils::FromBE(id)); }
static void WriteGalleryPrice(const Gallery::AmountWithAsset& x) { DocAddAidAmount("Price", x.m_Aid, x.m_Amount); }

static void OnMethod_Inner(uint32_t iMethod, const void* pArg, uint32_t nArg)
{
	switch (iMethod)
	{
	case 0: Env::DocAddText("method", "Create"); break;
	case 1: Env::DocAddText("method", "Destroy"); break;

	case Gallery::Method::AddExhibit::s_iMethod:
		if (nArg >= sizeof(Gallery::Method::AddExhibit))
		{
			auto* p = (const Gallery::Method::AddExhibit*) pArg;
			Env::DocAddText("method", "AddExhibit");
			Env::DocGroup gr("params");
			DocAddPk("pkUser", p->m_pkArtist);
			Env::DocAddNum("size", p->m_Size);
		}
		break;

	case Gallery::Method::ManageArtist::s_iMethod:
		if (nArg >= sizeof(Gallery::Method::ManageArtist))
		{
			auto* p = (const Gallery::Method::ManageArtist*) pArg;
			Env::DocAddText("method", "ManageArtist");
			Env::DocGroup gr("params");
			DocAddPk("pkUser", p->m_pkArtist);
			DocAddTextLen<Gallery::Artist::s_LabelMaxLen>("name", p + 1, p->m_LabelLen);
		}
		break;

	case Gallery::Method::SetPrice::s_iMethod:
		if (nArg >= sizeof(Gallery::Method::SetPrice))
		{
			auto* p = (const Gallery::Method::SetPrice*) pArg;
			Env::DocAddText("method", "SetPrice");
			Env::DocGroup gr("params");
			WriteGalleryAdrID(p->m_ID);
			WriteGalleryPrice(p->m_Price);
		}
		break;

	case Gallery::Method::Buy::s_iMethod:
		if (nArg >= sizeof(Gallery::Method::Buy))
		{
			auto* p = (const Gallery::Method::Buy*) pArg;
			Env::DocAddText("method", "Buy");
			Env::DocGroup gr("params");
			WriteGalleryAdrID(p->m_ID);
			DocAddPk("pkUser", p->m_pkUser);
			Env::DocAddNum32("hasAid", p->m_HasAid);
			DocAddAmount("payMax", p->m_PayMax);
		}
		break;

	case Gallery::Method::Transfer::s_iMethod:
		if (nArg >= sizeof(Gallery::Method::Transfer))
		{
			auto* p = (const Gallery::Method::Transfer*) pArg;
			Env::DocAddText("method", "Transfer");
			Env::DocGroup gr("params");
			WriteGalleryAdrID(p->m_ID);
			DocAddPk("newPkUser", p->m_pkNewOwner);
		}
		break;

	case Gallery::Method::Withdraw::s_iMethod:
		if (nArg >= sizeof(Gallery::Method::Withdraw))
		{
			auto* p = (const Gallery::Method::Withdraw*) pArg;
			Env::DocAddText("method", "Withdraw");
			Env::DocGroup gr("params");
			DocAddPk("key", p->m_Key.m_pkUser);
			DocAddAidAmount("Value", p->m_Key.m_Aid, p->m_Value);
		}
		break;

	case Gallery::Method::AddVoteRewards::s_iMethod:
		if (nArg >= sizeof(Gallery::Method::AddVoteRewards))
		{
			auto* p = (const Gallery::Method::AddVoteRewards*) pArg;
			Env::DocAddText("method", "AddVoteRewards");
			Env::DocGroup gr("params");
			DocAddAmount("amount", p->m_Amount);
		}
		break;

	case Gallery::Method::Vote::s_iMethod:
		if (nArg >= sizeof(Gallery::Method::Vote))
		{
			auto* p = (const Gallery::Method::Vote*) pArg;
			Env::DocAddText("method", "Vote");
			Env::DocGroup gr("params");
			WriteGalleryAdrID(p->m_ID.m_MasterpieceID);
			Env::DocAddNum("impression", p->m_Impression.m_Value);
		}
		break;

	case Gallery::Method::AdminDelete::s_iMethod:
		if (nArg >= sizeof(Gallery::Method::AdminDelete))
		{
			auto* p = (const Gallery::Method::AdminDelete*) pArg;
			Env::DocAddText("method", "AdminDelete");
			Env::DocGroup gr("params");
			WriteGalleryAdrID(p->m_ID);
		}
		break;
	}
}

BEAM_EXPORT uint32_t Method_3(ShaderID* out_buf, uint32_t out_cap)
{
	return ParserModule_FillSids(out_buf, out_cap, Gallery::s_pSID, _countof(Gallery::s_pSID));
}
BEAM_EXPORT void Method_0(const ShaderID& sid, const ContractID&, uint32_t iMethod, const void* pArg, uint32_t nArg)
{
	Env::DocGroup gr(""); OnKind(sid);
	OnMethod_Inner(iMethod, pArg, nArg);
}
BEAM_EXPORT void Method_1(const ShaderID& sid, const ContractID&) { Env::DocGroup gr(""); OnKind(sid); }
BEAM_EXPORT void Method_2(const ShaderID& sid, const ContractID&) { Env::DocGroup gr(""); OnKind(sid); }
