#include "../common.h"
#include "../app_common_impl.h"
#include "contract.h"

namespace
{
    uint32_t ReadAid(const ContractID& cid)
    {
        Env::Key_T<uint32_t> key;
        key.m_KeyInContract = 0;
        key.m_Prefix.m_Cid = cid;

        uint32_t aid;
        Env::VarReader::Read_T(key, aid);
        return aid;
    }

    void ReadImportedMsg(Bridge::InMsg& msg, const ContractID& cid)
    {
        Env::Key_T<uint32_t> key;
        key.m_KeyInContract = 1;
        key.m_Prefix.m_Cid = cid;

        Env::VarReader::Read_T(key, msg);
    }
} // namespace

namespace manager
{
    void Create()
    {
        FundsChange fc;
        fc.m_Aid = 0; // asset id
        fc.m_Amount = 300000000000ULL; // 3K beams; // amount of the input or output
        fc.m_Consume = 1; // contract consumes funds (i.e input, in this case)
        Env::GenerateKernel(nullptr, 0, nullptr, 0, &fc, 1, nullptr, 0, "create Bridge contract", 0);
    }

    void View()
    {
        EnumAndDumpContracts(Bridge::s_SID);
    }

    void ImportMsg(const ContractID& cid, uint32_t amount, const PubKey& pk)
    {
        Bridge::InMsg arg;

        arg.m_Amount = amount;
        arg.m_Pk = pk;
        arg.m_Finalized = 0;

        Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), nullptr, 0, nullptr, 0, "Import message", 0);
    }

    void ExportMsg(const ContractID& cid)
    {

    }

    void FinalizeMsg(const ContractID& cid)
    {
        Bridge::Finalized arg;
        Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), nullptr, 0, nullptr, 0, "Finalize message", 0);
    }

    void Unlock(const ContractID& cid/*, uint32_t aid, uint32_t amount*/)
    {
        Bridge::InMsg msg;

        ReadImportedMsg(msg, cid);

        FundsChange fc;
        fc.m_Aid = ReadAid(cid); // asset id
        fc.m_Amount = msg.m_Amount; // amount of the input or output
        fc.m_Consume = 0; // contract consumes funds (i.e input, in this case)
        

        Bridge::Unlock arg;
        /*arg.m_Amount = amount;
        Env::DerivePk(arg.m_Pk, &cid, sizeof(cid));*/

        SigRequest sig;
        sig.m_pID = &cid;
        sig.m_nID = sizeof(cid);

        Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), &fc, 1, &sig, 1, "Mint", 100000000);
        //Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), &fc, 1, nullptr, 0, "Mint", 0);
    }

    void Lock(const ContractID& cid, uint32_t aid, uint32_t amount)
    {
        FundsChange fc;
        fc.m_Aid = aid; // asset id
        fc.m_Amount = amount; // amount of the input or output
        fc.m_Consume = 1; // contract consumes funds (i.e input, in this case)


        Bridge::Lock arg;
        arg.m_Amount = amount;

        Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), &fc, 1, nullptr, 0, "Lock", 0);
    }

    void GeneratePK(const ContractID& cid)
    {
        PubKey pubKey;
        Env::DerivePk(pubKey, &cid, sizeof(cid));

        Env::DocAddBlob_T("pubkey", pubKey);
    }

    void GetAid(const ContractID& cid)
    {
        /*Env::Key_T<uint32_t> key;
        key.m_KeyInContract = 0;
        key.m_Prefix.m_Cid = cid;

        uint32_t aid;
        Env::VarReader::Read_T(key, aid);*/

        Env::DocAddNum32("aid", ReadAid(cid));
    }
} // namespace manager

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}

export void Method_0()
{
    // scheme
    Env::DocGroup root("");
    {
        Env::DocGroup gr("roles");
        {
            Env::DocGroup grRole("manager");
            {
                Env::DocGroup grMethod("create");
            }
            {
                Env::DocGroup grMethod("view");
            }
            {
                Env::DocGroup grMethod("importMsg");
                Env::DocAddText("cid", "ContractID");
                Env::DocAddText("amount", "uint32");
                Env::DocAddText("pubkey", "PubKey");
            }
            {
                Env::DocGroup grMethod("exportMsg");
                Env::DocAddText("cid", "ContractID");
            }
            {
                Env::DocGroup grMethod("finalizeMsg");
                Env::DocAddText("cid", "ContractID");
            }
            {
                Env::DocGroup grMethod("unlock");
                Env::DocAddText("cid", "ContractID");
                /*Env::DocAddText("aid", "uint32");
                Env::DocAddText("amount", "uint32");*/
            }
            {
                Env::DocGroup grMethod("lock");
                Env::DocAddText("cid", "ContractID");
                Env::DocAddText("aid", "uint32");
                Env::DocAddText("amount", "uint32");
            }
            {
                Env::DocGroup grMethod("generatePK");
                Env::DocAddText("cid", "ContractID");
            }
            {
                Env::DocGroup grMethod("getAid");
                Env::DocAddText("cid", "ContractID");
            }
        }
    }
}

export void Method_1()
{
    Env::DocGroup root("");

    char szRole[0x10], szAction[0x10];

    if (!Env::DocGetText("role", szRole, sizeof(szRole)))
        return OnError("Role not specified");

    if (!Env::DocGetText("action", szAction, sizeof(szAction)))
        return OnError("Action not specified");

    if (!Env::Strcmp(szRole, "manager"))
    {
        if (!Env::Strcmp(szAction, "create"))
        {
            manager::Create();
            return;
        }
        if (!Env::Strcmp(szAction, "view"))
        {
            manager::View();
            return;
        }
        if (!Env::Strcmp(szAction, "importMsg"))
        {
            ContractID cid;
            Env::DocGet("cid", cid);
            uint32_t amount = 0;
            Env::DocGetNum32("amount", &amount);
            PubKey pk;
            Env::DocGet("pubkey", pk);
            manager::ImportMsg(cid, amount, pk);
            return;
        }
        if (!Env::Strcmp(szAction, "exportMsg"))
        {
            ContractID cid;
            Env::DocGet("cid", cid);
            manager::ExportMsg(cid);
            return;
        }
        if (!Env::Strcmp(szAction, "finalizeMsg"))
        {
            ContractID cid;
            Env::DocGet("cid", cid);
            manager::FinalizeMsg(cid);
            return;
        }
        if (!Env::Strcmp(szAction, "unlock"))
        {
            ContractID cid;
            Env::DocGet("cid", cid);
            /*uint32_t amount = 0;
            Env::DocGetNum32("amount", &amount);
            uint32_t aid = 0;
            Env::DocGetNum32("aid", &aid);*/
            manager::Unlock(cid/*, aid, amount*/);
            return;
        }
        if (!Env::Strcmp(szAction, "lock"))
        {
            ContractID cid;
            Env::DocGet("cid", cid);
            uint32_t amount = 0;
            Env::DocGetNum32("amount", &amount);
            uint32_t aid = 0;
            Env::DocGetNum32("aid", &aid);
            manager::Lock(cid, aid, amount);
            return;
        }
        if (!Env::Strcmp(szAction, "generatePK"))
        {
            ContractID cid;
            Env::DocGet("cid", cid);
            manager::GeneratePK(cid);
            return;
        }
        if (!Env::Strcmp(szAction, "getAid"))
        {
            ContractID cid;
            Env::DocGet("cid", cid);
            manager::GetAid(cid);
            return;
        }
    }
}