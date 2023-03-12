#include "../common.h"
#include "../app_common_impl.h"

#define Dummy_get_Sid(macro) \
    macro(uint32_t, printBody)

#define DummyActions_All(macro) \
    macro(get_Sid)

BEAM_EXPORT void Method_0()
{
    // scheme
    Env::DocGroup root("");

    {   Env::DocGroup gr("actions");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_ACTION(name) { Env::DocGroup gr(#name);  Dummy_##name(THE_FIELD) }
        
        DummyActions_All(THE_ACTION)
#undef THE_ACTION
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) const type& name,
#define ON_METHOD(name) void On_##name(Dummy_##name(THE_FIELD) int unused = 0)

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}

struct Printer
{
    static const uint32_t s_CharsPerByte = 5;

    static void PrintBytesWithCommas(char* sz, const uint8_t* p, uint32_t n)
    {
        for (uint32_t i = 0; i < n; i++)
        {
            sz[0] = '0';
            sz[1] = 'x';
            Utils::String::Hex::Print(sz + 2, p[i], 2);
            sz[4] = ',';
            sz += s_CharsPerByte;
        }
    }
};


ON_METHOD(get_Sid)
{
    Utils::Shader sh;
    if (!sh.FromArg())
        return OnError("shader not specified");
    ShaderID sid;
    sh.get_Sid(sid);

    Env::DocGroup root("res");
    Env::DocAddBlob_T("sid", sid);

    // also print as formatted text
    {
        char szBuf[sizeof(sid) * Printer::s_CharsPerByte + 1];
        Printer::PrintBytesWithCommas(szBuf, sid.m_p, sizeof(sid));
        szBuf[_countof(szBuf) - 2] = 0; // remove trailing comma

        Env::DocAddText("sid-txt", szBuf);
    }

    if (printBody)
    {
        const uint32_t nMaxPerLine = 16;

        uint32_t nFullLines = sh.m_n / nMaxPerLine;
        uint32_t nLen = sh.m_n * Printer::s_CharsPerByte + nFullLines;
        char* sz = (char*) Env::Heap_Alloc(nLen);

        uint32_t nRes = 0;
        for (uint32_t i = 0; i < sh.m_n; )
        {
            uint32_t nNaggle = sh.m_n - i;
            if (nNaggle > nMaxPerLine)
                nNaggle = nMaxPerLine;

            Printer::PrintBytesWithCommas(sz + nRes, reinterpret_cast<const uint8_t*>(sh.m_p) + i, nNaggle);
            nRes += nNaggle * Printer::s_CharsPerByte;

            if (nNaggle == nMaxPerLine)
                sz[nRes++] = '\n';

            i += nNaggle;
        }

        Env::Write(sz, nRes, 0);

        Env::Heap_Free(sz);
    }
}

#undef ON_METHOD
#undef THE_FIELD

BEAM_EXPORT void Method_1() 
{
    Env::DocGroup root("");

    char szAction[0x20];

    if (!Env::DocGetText("action", szAction, sizeof(szAction)))
        return OnError("Action not specified");

#define PAR_READ(type, name) type arg_##name; Env::DocGet(#name, arg_##name);
#define PAR_PASS(type, name) arg_##name,

#define THE_METHOD(name) \
        static_assert(sizeof(szAction) >= sizeof(#name)); \
        if (!Env::Strcmp(szAction, #name)) { \
            Dummy_##name(PAR_READ) \
            On_##name(Dummy_##name(PAR_PASS) 0); \
            return; \
        }

    DummyActions_All(THE_METHOD)

#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    OnError("unknown action");
}
