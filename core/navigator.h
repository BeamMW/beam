#pragma once
#include "common.h"
#ifdef WIN32
#include <windows.h>
#endif

namespace beam
{
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

		Offset get_Offset(const void* p) const;

		void* Allocate(uint32_t iBank, uint32_t nSize);
		void Free(uint32_t iBank, void*);
	};

	class ChainNavigator
	{
	protected:

		MappedFile m_Mapping;

		struct Type {
			enum Enum {
				Tag,
				count
			};
		};

	public:

		typedef ECC::Hash::Value TagType;
		typedef MappedFile::Offset Offset;

#pragma pack (push, 8)

		struct Links {
			Offset p[2];
		};

		struct TagInfo
		{
			Height		m_Height;
			Difficulty	m_Difficulty;
			TagType		m_Tag;

			void ModifyBy(const TagInfo&, bool bFwd);
		};

		struct TagMarker
		{
			TagInfo m_Diff;

			Links m_Links;		// next/prev
			Links m_Patches;	// head/tail
			Offset m_Child0;
			Offset m_Parent;
		};

		struct Patch {
			Links m_Links;		// next/prev
		};

		struct FixedHdr
		{
			TagMarker	m_Root;
			Offset		m_TagCursor; // current tag
			TagInfo		m_TagInfo;
		};

#pragma pack (pop)

		void Open(const char* sz);
		void Close();


		// Interface
		const FixedHdr& get_Hdr() const { return get_Hdr_(); }
		const TagMarker& get_Tag(Offset x) const { return get_Tag_(x); }

		Offset get_ChildTag() const;
		Offset get_ChildTag(Offset) const;
		Offset get_NextTag(Offset) const;

		void MoveFwd(Offset);
		bool MoveBwd();

		void Commit(Patch&, bool bApply = true);
		void CreateTag(const TagInfo&, bool bMoveTo = true);

		void DeleteTag(Offset); // can't be root. If has children - changes are applied to children

		void assert_valid() const; // diagnostic, for tests only

	protected:

		TagMarker& get_Tag_(Offset) const;
		Patch& get_Patch_(Offset) const;
		FixedHdr& get_Hdr_() const { return *(FixedHdr*) m_Mapping.get_FixedHdr(); }

		void ApplyTagChanges(Offset, bool bFwd);
		void MovePatchesToChildren(Offset xTag);

		void PatchListOperate(TagMarker&, Patch&, bool bAdd);

		template <class T>
		void ListOperate(T& node, bool bAdd, Offset* pE, int nE);

		template <class T>
		void ListOperateDir(T& node, Offset n, int iDir, bool bAdd, Offset* pE, int nE);

		void assert_valid(const TagMarker&, bool& bCursorHit) const;

		virtual void AdjustDefs(MappedFile::Defs&) {}
		virtual void OnOpen() {}
		virtual void OnClose() {}
		virtual void Delete(Patch&) {}
		virtual void Apply(const Patch&, bool bFwd) = 0;
		virtual Patch* Clone(Offset) = 0;
		virtual void assert_valid(bool b) const { assert(b); }
	};
}
