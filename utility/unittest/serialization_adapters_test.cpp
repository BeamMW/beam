#include "core/common.h"
#include "core/serialization_adapters.h"

int main()
{
    // test for the ECC::Point adapter
    {
        ECC::Point in;
        in.m_X.m_Value.m_pData[0] = 123;
        in.m_bQuadraticResidue = true;

        beam::Serializer ser;
        ser & in;

        auto [buf, size] = ser.buffer();

        beam::Deserializer des;
        des.reset(buf, size);

        ECC::Point out;
        des & out;

        assert(in.m_X.m_Value == out.m_X.m_Value);
        assert(in.m_bQuadraticResidue == out.m_bQuadraticResidue);
    }

    // and etc...

    return 0;
}
