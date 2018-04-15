#pragma once
#include "common.h"


namespace beam
{
	void test_SysRet(bool bFail);

	class MappedFile
	{
	public:
		typedef uint64_t Offset;

	private:
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

		void Open(const char* sz, const Defs&);
		void Close();

		void* get_FixedHdr() const;

		template <typename T> T& get_At(Offset n) const
		{
			assert(m_pMapping && (m_nMapping >= n + sizeof(T)));
			return *(T*) (m_pMapping  + n);
		}

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
