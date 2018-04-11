#include "core/common.h"
#include "core/serialization_adapters.h"

int main()
{
    // test for the ECC::Point adapter
    {
        ECC::Point in;
        in.m_X.m_pData[0] = 123;
        in.m_Y = true;

        beam::Serializer ser;
        ser & in;

        auto [buf, size] = ser.buffer();

        beam::Deserializer des;
        des.reset(buf, size);

        ECC::Point out;
        des & out;

        assert(in.m_X == out.m_X);
        assert(in.m_Y == out.m_Y);
    }

    // and etc...

    return 0;
}
