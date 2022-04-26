#pragma once
namespace Upgradable3
{
    namespace Test
    {

#pragma pack (push, 1) // the following structures will be stored in the node in binary form

        namespace Method
        {
            struct Create
            {
                static const uint32_t s_iMethod = 0;
                Settings m_Stgs;
            };

            struct Some
            {
                static const uint32_t s_iMethod = 3;
                uint32_t m_ExpectedVer;
            };
        }

#pragma pack (pop)

    }
}
