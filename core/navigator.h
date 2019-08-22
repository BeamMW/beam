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
#include "block_crypt.h"
#include "mapped_file.h"

namespace beam
{
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
