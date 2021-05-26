// Copyright 2018-2021 The Beam Team
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

#pragma once

#include "utility/common.h"
#include <cstdint>
#include <vector>

namespace beam
{
	namespace EthashUtils
	{
		void GenerateLocalCache(uint32_t iEpoch, const char* szPath);

		void GenerateLocalData(uint32_t iEpoch, const char* szPathCache, const char* szPathMerkle, uint32_t h0);

		void GenerateSuperTree(const char* szRes, const char* szPathCache, const char* szPathMerkle, uint32_t h0);

		void CropLocalData(const char* szDst, const char* szSrc, uint32_t dh);

		uint32_t GenerateProof(uint32_t iEpoch, const char* szPathCache, const char* szPathMerkle, const char* szPathSuperTree, const uintBig_t<64>& hvSeed, ByteBuffer& res);

	} // namespace EthashUtils
}