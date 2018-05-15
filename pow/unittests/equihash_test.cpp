#include "core/common.h"
#include <iostream>

int main()
{
    uint8_t pInput[] = {1, 2, 3, 4, 56};

	const beam::Difficulty d = 0; // d=0, runtime ~48 sec. d=1,2 - almost close to this. d=4 - runtime 4 miuntes, several cycles until solution is achieved.

	beam::Block::PoW pow;
	pow.m_Nonce = 0x010204U;
	pow.Solve(pInput, sizeof(pInput), d);

    if (!pow.IsValid(pInput, sizeof(pInput), d))
		return -1;

    std::cout << "Solution is correct\n";
    return 0;
}
