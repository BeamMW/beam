#include "../common.h"
#include "contract.h"
#include "../BeamHeader.h"

void SetPerHeight(Sidechain::PerHeight& ph, const BlockHeader::Full& s, const Sidechain::Global& g)
{
    Env::Halt_if(g.m_VerifyPoW && !s.IsValid(&g.m_Rules));

    s.get_Hash(ph.m_Hash, &g.m_Rules);
    _POD_(ph.m_Kernels) = s.m_Kernels;
    //_POD_(ph.m_Definition) = s.m_Definition;
    ph.m_Timestamp = s.m_Timestamp;
    ph.m_Difficulty = s.m_PoW.m_Difficulty;
}

export void Ctor(const Sidechain::Init& r)
{
    Sidechain::Global g;
    _POD_(Cast::Down<Sidechain::Immutable>(g)) = Cast::Down<Sidechain::Immutable>(r);
    g.m_Chainwork.FromBE_T(r.m_Hdr0.m_ChainWork);
    g.m_Height = r.m_Hdr0.m_Height;
    Env::SaveVar_T((uint8_t) 0, g);

    Sidechain::PerHeight ph;
    SetPerHeight(ph, r.m_Hdr0, g);
    _POD_(ph.m_Contributor).SetZero();
    Env::SaveVar_T(r.m_Hdr0.m_Height, ph);
}

export void Dtor(void*)
{
    // n/a
}

export void Method_2(const Sidechain::Grow<0>& r)
{
    Sidechain::Global g;
    Env::LoadVar_T((uint8_t) 0, g);

    Env::Halt_if((r.m_nSequence < 1) || (r.m_Prefix.m_Height > g.m_Height + 1));

    Height hPrev = r.m_Prefix.m_Height - 1;
    Sidechain::PerHeight ph0;
    Env::Halt_if(!Env::LoadVar_T(hPrev, ph0) || (_POD_(ph0.m_Hash) != r.m_Prefix.m_Prev));

    BlockHeader::Full s;
    _POD_(Cast::Down<BlockHeader::Prefix>(s)) = r.m_Prefix;
    _POD_(Cast::Down<BlockHeader::Element>(s)) = r.m_pSequence[0];

    BeamDifficulty::Raw cw0, cw1, cw2;
    cw0.FromBE_T(s.m_ChainWork);
    _POD_(cw2) = cw0; // chainwork of the new branch (to be incremented)
    BeamDifficulty::Unpack(cw1, s.m_PoW.m_Difficulty);
    cw0 -= cw1; // chainwork up to prev

    Sidechain::PerHeight ph;
    _POD_(ph.m_Contributor) = r.m_Contributor;

    for (uint32_t iHdr = 0; ; )
    {
        SetPerHeight(ph, s, g);
        bool bSave = true;

        if (s.m_Height <= g.m_Height)
        {
            Sidechain::PerHeight phOld;
            Env::LoadVar_T(s.m_Height, phOld);

            if (_POD_(phOld.m_Hash) == ph.m_Hash)
                bSave = false;

            BeamDifficulty::Unpack(cw1, phOld.m_Difficulty);
            cw0 += cw1;
        }

        if (bSave)
            Env::SaveVar_T(s.m_Height, ph);

        if (++iHdr == r.m_nSequence)
            break;

        // advance
        s.m_Height++;
        _POD_(s.m_Prev) = ph.m_Hash;
        _POD_(Cast::Down<BlockHeader::Element>(s)) = r.m_pSequence[iHdr];

        BeamDifficulty::Unpack(cw1, s.m_PoW.m_Difficulty);
        cw2 += cw1;
        cw2.ToBE_T(s.m_ChainWork);
    }

    for (Height h = s.m_Height; h < g.m_Height; )
    {
        // this is needed for the rare case where after reorg the height is lower (yet the chainwork may be bigger)
        Sidechain::PerHeight phOld;
        Env::LoadVar_T(++h, phOld);
        BeamDifficulty::Unpack(cw1, phOld.m_Difficulty);
        cw0 += cw1;

        Env::DelVar_T(h);
    }

    Env::Halt_if(
        (cw0 != g.m_Chainwork) || // initial chainwork was incorrect
        (cw2 <= g.m_Chainwork) // overall chainwork not increased
    ); 

    // looks good
    _POD_(g.m_Chainwork) = cw2;
    g.m_Height = s.m_Height;
    Env::SaveVar_T((uint8_t) 0, g);
}

Amount ContributorLoad(const PubKey& key)
{
    Amount val;
    return Env::LoadVar_T(key, val) ? val : 0;
}

void ContributorSave(const PubKey& key, Amount val)
{
    if (val)
        Env::SaveVar_T(key, val);
    else
        Env::DelVar_T(key);
}

export void Method_3(const Sidechain::VerifyProof<0>& r)
{
    Sidechain::PerHeight ph;
    Env::Halt_if(!Env::LoadVar_T(r.m_Height, ph));

    HashValue hv;
    _POD_(hv) = r.m_KernelID;
    Merkle::Interpret(hv, r.m_pProof, r.m_nProof);

    Env::Halt_if(_POD_(hv) != ph.m_Kernels);

    Sidechain::Global g;
    Env::LoadVar_T((uint8_t) 0, g);

    if (g.m_ComissionForProof)
    {
        Amount val = ContributorLoad(ph.m_Contributor);
        Strict::Add(val, g.m_ComissionForProof);
        ContributorSave(ph.m_Contributor, val);

        Env::FundsLock(0, g.m_ComissionForProof);
    }
}

export void Method_4(const Sidechain::WithdrawComission& r)
{
    Env::Halt_if(!r.m_Amount);

    Amount val = ContributorLoad(r.m_Contributor);
    Strict::Sub(val, r.m_Amount);
    ContributorSave(r.m_Contributor, val);

    Env::FundsUnlock(0, r.m_Amount);
    Env::AddSig(r.m_Contributor);
}
