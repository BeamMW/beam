#pragma once
#include "common.h"


namespace beam
{
	void test_SysRet(bool bFail);

	class MappedFile
	{
		typedef uint64_t Offset;

		struct Bank {
			Offset m_Tail;
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
		bool TestSig(const uint8_t* pSig, uint32_t nSizeSig);
		Bank& get_Bank(uint32_t iBank);

	public:

		MappedFile();
		~MappedFile();

		void Open(const char* sz, const uint8_t* pSig, uint32_t nSizeSig, uint32_t nBanks);
		void Close();

		void* Allocate(uint32_t iBank, uint32_t nSize);
		void Free(uint32_t iBank, void*);
	};
/*
	class ChainNavigator
	{
	public:

		typedef ECC::Hash::Value TagType;

		struct Patch {
		};

		ChainNavigator(const char* szPath);

		TagType	m_Tag;
		Height	m_Height;

		void Commit(const Patch&);
		void Tag(const TagType&);

	private:
		virtual uint32_t get_Size(const Patch&) = 0;
		virtual void Apply(const Patch&, bool bReverse) = 0;
	};*/
}
