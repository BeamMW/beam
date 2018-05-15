#include "core/common.h"
#include <iostream>

int main()
{
    uint8_t pInput[] = {1, 2, 3, 4, 56};

	beam::Block::PoW pow;
	pow.m_Nonce = 0x010204U;
	pow.Solve(pInput, sizeof(pInput));

    if (pow.IsValid(pInput, sizeof(pInput)))
    {
        std::cout << "Solution is correct\n";
        return 0;
    }
    return -1;
}
