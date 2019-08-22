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

#pragma once
#include "common.h"

namespace beam
{
	class MappedFile
	{
	public:
		typedef uint64_t Offset;

	private:
		struct Bank
		{
			Offset m_Tail;
			uint64_t m_Total;
			uint64_t m_Free;
		};

		static uint32_t s_PageSize;

#ifdef WIN32
		HANDLE m_hFile;
		HANDLE m_hMapping;
#else // WIN32
		int m_hFile;
#endif // WIN32

		Offset m_nMapping;
		uint8_t* m_pMapping;
		uint32_t m_nBank0;
		uint32_t m_nBanks;

		void ResetVarsFile();
		void ResetVarsMapping();
		void CloseMapping();
		void OpenMapping();
		//void Write(const void*, uint32_t);
		//void WriteZero(uint32_t);
		void Resize(Offset);
		Bank& get_Bank(uint32_t iBank);

	public:

		MappedFile();
		~MappedFile();

		struct Defs
		{
			const uint8_t* m_pSig;
			uint32_t m_nSizeSig;
			uint32_t m_nBanks;
			uint32_t m_nFixedHdr;

			uint32_t get_Bank0() const;
			uint32_t get_SizeMin() const;
		};

		void Open(const char* sz, const Defs&, bool bReset = false);
		void Close();

		void* get_FixedHdr() const;

		template <typename T> T& get_At(Offset n) const
		{
			assert(m_pMapping && (m_nMapping >= n + sizeof(T)));
			return *(T*) (m_pMapping  + n);
		}

		Offset get_Offset(const void* p) const;
		const uint8_t* get_Base() const { return m_pMapping; }

		void* Allocate(uint32_t iBank, uint32_t nSize);
		void Free(uint32_t iBank, void*);

		void EnsureReserve(uint32_t iBank, uint32_t nSize, uint32_t nMinFree);
	};

} // namespace beam
