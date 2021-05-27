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

#ifndef HOST_BUILD
#define HOST_BUILD
#endif

#include "ethash_utils.h"
#include "core/mapped_file.h"
#include "core/ecc_native.h"
#include "core/merkle.h"
#include "core/block_crypt.h"
#include "utility/byteorder.h"
#include "ethash/include/ethash/ethash.h"
#include "ethash/lib/ethash/ethash-internal.hpp"

#include "shaders_ethash.h"

namespace beam::EthashUtils
{
	namespace
	{
		using ProofBase = Shaders::Ethash::ProofBase;

		void EvaluateElement(ProofBase::THash& hv, ProofBase::TCount n, const ethash_epoch_context& ctx)
		{
			auto item = ethash::calculate_dataset_item_1024(ctx, n);

			ECC::Hash::Processor()
				<< Blob(item.bytes, sizeof(item.bytes))
				>> hv;
		}

		struct Hdr
		{
			uint32_t m_FullItems;
			uint32_t m_h0; // height from which the hashes are stored
			// followed by the tree hashes
		};

		struct HdrCache
		{
			uint32_t m_FullItems;
			uint32_t m_CacheItems;
			// followed by the cache
		};

		struct MyMultiProofBase
			:public Shaders::Ethash::ProofBase
		{
			std::vector<THash> m_vRes;

			void ProofPushZero()
			{
				m_vRes.emplace_back() = Zero;
			}

			void ProofMerge()
			{
				assert(m_vRes.size() >= 2);
				auto& hv = m_vRes[m_vRes.size() - 2];

				ECC::Hash::Processor() << hv << m_vRes.back() >> hv;
				m_vRes.pop_back();
			}

			static void EvaluateEpoch(THash& hv, const THash& hvEpochRoot, uint32_t nEpochElements)
			{
				ECC::Hash::Processor()
					<< hvEpochRoot
					<< nEpochElements
					>> hv;
			}
		};

		struct MyMultiProof
			:public MyMultiProofBase
		{
			const THash* m_pHashes;
			const Hdr* m_pHdr;
			const ethash_epoch_context* m_pCtx;

			bool ProofPush(TCount n, TCount nHalf)
			{
				Merkle::Position pos;

				for (pos.H = 0; nHalf; pos.H++)
				{
					nHalf >>= 1;
					assert(!(1 & n));
					n >>= 1;
				}

				pos.X = n;

				uint8_t h0 = static_cast<uint8_t>(ByteOrder::from_le(m_pHdr->m_h0));
				if (pos.H < h0)
				{
					if (pos.H)
						return false;

					EvaluateElement(m_vRes.emplace_back(), n, *m_pCtx);
				}
				else
				{
					auto iHash = Merkle::FlatMmr::Pos2Idx(pos, h0);
					m_vRes.push_back(m_pHashes[iHash]);
				}

				return true;
			}

		};

		ethash_epoch_context ReadLocalCache(MappedFileRaw& fmp, const char* szPath)
		{
			fmp.Open(szPath);

			auto& hdr = fmp.get_At<HdrCache>(0);

			return ethash_epoch_context{
				0,
				static_cast<int>(ByteOrder::from_le(hdr.m_CacheItems)),
				&fmp.get_At<ethash_hash512>(sizeof(Hdr)),
				nullptr,
				static_cast<int>(ByteOrder::from_le(hdr.m_FullItems)) };
		}

		using MyBuilder = Shaders::MultiProof::Builder<MyMultiProof>;
	}

	void GenerateLocalCache(uint32_t iEpoch, const char* szPath)
	{
		ethash_epoch_context* pCtx = ethash_create_epoch_context(iEpoch);

		uint32_t nFullItems = pCtx->full_dataset_num_items;

		HdrCache hdr;
		hdr.m_FullItems = ByteOrder::to_le(nFullItems);
		hdr.m_CacheItems = ByteOrder::to_le((uint32_t)pCtx->light_cache_num_items);

		std::FStream fs;
		fs.Open(szPath, false, true);
		fs.write(&hdr, sizeof(hdr));

		fs.write(pCtx->light_cache, sizeof(*pCtx->light_cache) * pCtx->light_cache_num_items);

		ethash_destroy_epoch_context(pCtx);
	}

	void GenerateLocalData(uint32_t iEpoch, const char* szPathCache, const char* szPathMerkle, uint32_t h0)
	{
		GenerateLocalCache(iEpoch, szPathCache);

		MappedFileRaw fmpCache;
		auto ctx = ReadLocalCache(fmpCache, szPathCache);

		uint32_t nFullItems = ctx.full_dataset_num_items;

		Hdr hdr;
		hdr.m_FullItems = ByteOrder::to_le(nFullItems);
		hdr.m_h0 = ByteOrder::to_le(h0);

		std::FStream fs;
		fs.Open(szPathMerkle, false, true);
		fs.write(&hdr, sizeof(hdr));

		MyMultiProofBase wrk;

		for (uint32_t i = 0; i < nFullItems; )
		{
			EvaluateElement(wrk.m_vRes.emplace_back(), i, ctx);

			uint32_t nPos = ++i;
			for (uint32_t h = 0; ; h++, nPos >>= 1)
			{
				if (h >= h0)
					fs.write(&wrk.m_vRes.back(), sizeof(ProofBase::THash));

				if (1 & nPos)
					break;

				wrk.ProofMerge();
			}
		}
	}

	void GenerateSuperTree(const char* szRes, const char* szPathCache, const char* szPathMerkle, uint32_t h0)
	{
		std::FStream fs;
		fs.Open(szRes, false, true);

		MyMultiProofBase wrk;

		for (uint32_t i = 0; i < ProofBase::nEpochsTotal; )
		{
			{
				std::string sPath = szPathCache;
				sPath += std::to_string(i) + ".cache";

				MappedFileRaw fmpCache;
				auto ctx = ReadLocalCache(fmpCache, sPath.c_str());

				sPath = szPathMerkle;
				sPath += std::to_string(i) + ".tre" + std::to_string(h0);

				
				MappedFileRaw fmpMerkle;
				fmpMerkle.Open(sPath.c_str());

				auto& hdrSrc = fmpMerkle.get_At<Hdr>(0);
				uint32_t nFullItems = ByteOrder::from_le(hdrSrc.m_FullItems);

				MyBuilder mpb;

				mpb.m_pHdr = &hdrSrc;
				mpb.m_pCtx = &ctx;
				mpb.m_pHashes = &fmpMerkle.get_At<MyBuilder::THash>(sizeof(Hdr));

				mpb.Build(nullptr, 0, ctx.full_dataset_num_items); // root

				mpb.EvaluateEpoch(wrk.m_vRes.emplace_back(), mpb.m_vRes.front(), nFullItems);
			}

			uint32_t nPos = ++i;
			for (uint32_t h = 0; ; h++, nPos >>= 1)
			{
				fs.write(&wrk.m_vRes.back(), sizeof(ProofBase::THash));

				if (1 & nPos)
					break;

				wrk.ProofMerge();
			}
		}
	}

	void CropLocalData(const char* szDst, const char* szSrc, uint32_t dh)
	{
		MappedFileRaw fmp;
		fmp.Open(szSrc);

		auto& hdrSrc = fmp.get_At<Hdr>(0);
		uint32_t nFullItems = ByteOrder::from_le(hdrSrc.m_FullItems);
		uint32_t h0 = ByteOrder::from_le(hdrSrc.m_h0);

		auto pHashes = &fmp.get_At<ProofBase::THash>(sizeof(Hdr));

		Hdr hdrDst = hdrSrc;
		hdrDst.m_h0 = ByteOrder::to_le(h0 + dh);

		std::FStream fs;
		fs.Open(szDst, false, true);
		fs.write(&hdrDst, sizeof(hdrDst));

		uint32_t nHashes0 = static_cast<uint32_t>(Merkle::FlatMmr::get_TotalHashes(nFullItems, static_cast<uint8_t>(h0)));
		Merkle::Position pos;
		ZeroObject(pos);

		for (uint32_t i = 0; i < nHashes0; i++)
		{
			if (pos.H >= dh)
				fs.write(pHashes + i, sizeof(ProofBase::THash));

			const uint32_t nMsk = (2U << pos.H) - 1;

			if (nMsk == (pos.X & nMsk))
				pos.H++;
			else
			{
				pos.H = 0;
				pos.X++;
			}
		}
	}

	uint32_t GenerateProof(uint32_t iEpoch, const char* szPathCache, const char* szPathMerkle, const char* szPathSuperTree, const uintBig_t<64>& hvSeed, ByteBuffer& res)
	{
		MappedFileRaw fmpCache, fmpMerkle, fmpSuperTree;
		fmpMerkle.Open(szPathMerkle);
		auto ctx = ReadLocalCache(fmpCache, szPathCache);

		auto& hdr = fmpMerkle.get_At<Hdr>(0);

		ECC::Hash::Value hvMix;
		uint32_t pSolIndices[64];
		ethash_hash1024 pSolItems[64];

		ethash_get_MixHash2((ethash_hash256*)hvMix.m_pData, pSolIndices, pSolItems, &ctx, (ethash_hash512*)hvSeed.m_pData);

		MyBuilder mpb;

		mpb.m_pHdr = &hdr;
		mpb.m_pCtx = &ctx;
		mpb.m_pHashes = &fmpMerkle.get_At<MyBuilder::THash>(sizeof(Hdr));

		mpb.Build(pSolIndices, _countof(pSolIndices), ctx.full_dataset_num_items); // proof for this set of indices

		fmpSuperTree.Open(szPathSuperTree);
		const auto* pSuper = &fmpSuperTree.get_At<MyBuilder::THash>(0);

		for (uint8_t h = 0; ; h++)
		{
			uint32_t nMsk = 1U << h;
			if (nMsk >= ProofBase::nEpochsTotal)
				break;

			Merkle::Position pos;
			pos.X = (iEpoch >> h) ^ 1;
			pos.H = h;

			mpb.m_vRes.push_back(pSuper[Merkle::FlatMmr::Pos2Idx(pos, 0)]);
		}

		res.resize(sizeof(pSolItems) + sizeof(ProofBase::THash) * mpb.m_vRes.size());
		memcpy(&res.front(), pSolItems, sizeof(pSolItems));
		memcpy(&res.front() + sizeof(pSolItems), &mpb.m_vRes.front(), sizeof(ProofBase::THash) * mpb.m_vRes.size());

		return ctx.full_dataset_num_items;
	}

} // namespace beam::EthashUtils
