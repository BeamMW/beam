#include "../common.h"
#include "contract.h"
#include "../BeamHeader.h"

void SetPerHeight(Sidechain::PerHeight& ph, const BeamHeaderFull& s, const Sidechain::Global& g)
{
    Env::Halt_if(g.m_VerifyPoW && !s.IsValid(&g.m_Rules));

    s.get_Hash(ph.m_Hash, &g.m_Rules);
    Utils::Copy(ph.m_Kernels, s.m_Kernels);
    //Utils::Copy(ph.m_Definition, s.m_Definition);
    ph.m_TimeStamp = s.m_TimeStamp;
    ph.m_Difficulty = s.m_PoW.m_Difficulty;
}

export void Ctor(const Sidechain::Init& r)
{
    Sidechain::Global g;
    Utils::Copy(g.m_Rules, r.m_Rules);
    g.m_Chainwork.FromBE_T(r.m_Hdr0.m_ChainWork);
    g.m_Height = r.m_Hdr0.m_Height;
    g.m_VerifyPoW = r.m_VerifyPoW;
    Env::SaveVar_T((uint8_t) 0, g);

    Sidechain::PerHeight ph;
    SetPerHeight(ph, r.m_Hdr0, g);
    Utils::ZeroObject(ph.m_Contributor);
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
    Env::Halt_if(!Env::LoadVar_T(hPrev, ph0) || Utils::Cmp(ph0.m_Hash, r.m_Prefix.m_Prev));

    BeamHeaderFull s;
    Utils::Copy(Cast::Down<BeamHeaderPrefix>(s), r.m_Prefix);
    Utils::Copy(Cast::Down<BeamHeaderSequence>(s), r.m_pSequence[0]);

    BeamDifficulty::Raw cw0, cw1, cw2;
    cw0.FromBE_T(s.m_ChainWork);
    Utils::Copy(cw2, cw0); // chainwork of the new branch (to be incremented)
    BeamDifficulty::Unpack(cw1, s.m_PoW.m_Difficulty);
    cw0 -= cw1; // chainwork up to prev

    Sidechain::PerHeight ph;
    Utils::Copy(ph.m_Contributor, r.m_Contributor);

    for (uint32_t iHdr = 0; ; )
    {
        SetPerHeight(ph, s, g);
        bool bSave = true;

        if (s.m_Height <= g.m_Height)
        {
            Sidechain::PerHeight phOld;
            Env::LoadVar_T(s.m_Height, phOld);

            if (!Utils::Cmp(phOld.m_Hash, ph.m_Hash))
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
        Utils::Copy(s.m_Prev, ph.m_Hash);
        Utils::Copy(Cast::Down<BeamHeaderSequence>(s), r.m_pSequence[iHdr]);

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
    Utils::Copy(g.m_Chainwork, cw2);
    g.m_Height = s.m_Height;
    Env::SaveVar_T((uint8_t) 0, g);
}
