#include "../common.h"
#include "../app_common_impl.h"

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}

BEAM_EXPORT void Method_0()
{
    ShaderID sid;
    if (!Utils::get_ShaderID_FromArg(sid))
        return OnError("contract shader must be specified");

    Env::DocAddBlob_T("sid", sid);

    ContractID cid;
    uint32_t nDeployedCount = 0;

    {
        Env::DocArray gr("contracts");

        WalkerContracts wlk;
        for (wlk.Enum(sid); wlk.MoveNext(); nDeployedCount++)
        {
            Env::DocGroup root("");

            Env::DocAddBlob_T("cid", wlk.m_Key.m_KeyInContract.m_Cid);
            Env::DocAddNum("Height", wlk.m_Height);

            _POD_(cid) = wlk.m_Key.m_KeyInContract.m_Cid;
        }
    }

    if (!nDeployedCount)
    {
        uint32_t nVal = 0;
        Env::DocGetNum32("deploy", &nVal);

        if (!nVal)
        {
            Env::DocAddText("res", "Not deployed. Specify deploy=1 to auto deploy");
            return;
        }

        Env::GenerateKernel(nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, "create playground contract", 0);

        Utils::get_Cid(cid, sid, nullptr, 0);
    }

    uint32_t nRun = 0;
    Env::DocGetNum32("run", &nRun);

    if (!nRun)
        return;

    if (nDeployedCount > 1)
    {
        if (!Env::DocGet("cid", cid))
        {
            Env::DocAddText("res", "cid is ambiguous. Please sepcify");
            return;
        }
    }

    Env::GenerateKernel(&cid, 2, nullptr, 0, nullptr, 0, nullptr, 0, "play in playground", 100000000);
}

BEAM_EXPORT void Method_1()
{
    Method_0(); // make no difference
}
