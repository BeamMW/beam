// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "core/block_crypt.h"
#include <iostream>

int main()
{
    uint8_t pInput[] = {1, 2, 3, 4, 56};

	beam::Block::PoW pow;
	pow.m_Difficulty = 0; // d=0, runtime ~48 sec. d=1,2 - almost close to this. d=4 - runtime 4 miuntes, several cycles until solution is achieved.
	pow.m_Nonce = 0x010204U;

    // our builder doesn't support GPU
//#if defined (BEAM_USE_GPU)
//    {
//	    pow.SolveGPU(pInput, sizeof(pInput));
//
//        if (!pow.IsValid(pInput, sizeof(pInput)))
//		    return -1;
//    }
//#else

    {
        pow.Solve(pInput, sizeof(pInput));

        if (!pow.IsValid(pInput, sizeof(pInput)))
            return -1;
    }

//#endif

    std::cout << "Solution is correct\n";
    return 0;
}
