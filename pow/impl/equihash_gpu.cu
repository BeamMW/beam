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

// Equihash CUDA solver
// Copyright (c) 2016 John Tromp

#include "equihash_gpu.h"
#include "crypto/common.h"

#include <string.h>

#include <vector>
#include <exception>
#include <sstream>

#include "cuda_runtime.h"
#include "device_launch_parameters.h"

namespace 
{

typedef uint32_t u32;
typedef unsigned char uchar;

#ifndef WN
#define WN	144
#endif

#ifndef WK
#define WK	5
#endif

#define NDIGITS		(WK+1)
#define DIGITBITS	(WN/(NDIGITS))

static const u32 PROOFSIZE = 1 << WK;
static const u32 BASE = 1 << DIGITBITS;
static const u32 NHASHES = 2 * BASE;
static const u32 HASHESPERBLAKE = 512 / WN;
static const u32 HASHOUT = HASHESPERBLAKE * WN / 8;

#define COMPRESSED_SOL_SIZE (PROOFSIZE * (DIGITBITS + 1) / 8)

typedef u32 proof[PROOFSIZE];

#include "blake2b.cu"

typedef uint16_t u16;
typedef uint64_t u64;

#define checkCudaErrors(ans) { gpuAssert((ans), __FILE__, __LINE__); }
inline void gpuAssert(cudaError_t code, const char *file, int line, bool abort = true) {
    if (code != cudaSuccess) {
        if (abort) {
            std::stringstream stream;

            stream << "GPUassert: " << cudaGetErrorString(code) << " " << file << " " << line;
            throw std::runtime_error(stream.str());
        }
    }
}

#ifndef RESTBITS
#define RESTBITS	4
#endif

// 2_log of number of buckets
#define BUCKBITS (DIGITBITS-RESTBITS)

#ifndef SAVEMEM
#if RESTBITS == 4
// can't save memory in such small buckets
#define SAVEMEM 1
#elif RESTBITS >= 8
// take advantage of law of large numbers (sum of 2^8 random numbers)
// this reduces (200,9) memory to under 144MB, with negligible discarding
#define SAVEMEM 9/14
#endif
#endif

// number of buckets
static const u32 NBUCKETS = 1 << BUCKBITS;
// 2_log of number of slots per bucket
static const u32 SLOTBITS = RESTBITS + 1 + 1;
static const u32 SLOTRANGE = 1 << SLOTBITS;
// number of slots per bucket
static const u32 NSLOTS = SLOTRANGE * SAVEMEM;
// SLOTBITS mask
static const u32 SLOTMASK = SLOTRANGE - 1;
// number of possible values of xhash (rest of n) bits
static const u32 NRESTS = 1 << RESTBITS;
// RESTBITS mask
static const u32 RESTMASK = NRESTS - 1;
// number of blocks of hashes extracted from single 512 bit blake2b output
static const u32 NBLOCKS = (NHASHES + HASHESPERBLAKE - 1) / HASHESPERBLAKE;
// nothing larger found in 100000 runs
static const u32 MAXSOLS = 10;

// tree node identifying its children as two different slots in
// a bucket on previous layer with the same rest bits (x-tra hash)
struct tree {
    u32 bid_s0_s1_x; // manual bitfields

    __device__ tree(const u32 idx, const u32 xh) {
        bid_s0_s1_x = idx << RESTBITS | xh;
    }
    __device__ tree(const u32 idx) {
        bid_s0_s1_x = idx;
    }
#ifdef XINTREE
    __device__ tree(const u32 bid, const u32 s0, const u32 s1, const u32 xh) {
        bid_s0_s1_x = ((((bid << SLOTBITS) | s0) << SLOTBITS) | s1) << RESTBITS | xh;
#else
    __device__ tree(const u32 bid, const u32 s0, const u32 s1) {
        bid_s0_s1_x = (((bid << SLOTBITS) | s0) << SLOTBITS) | s1;
#endif
    }
    __device__ u32 getindex() const {
#ifdef XINTREE
        return bid_s0_s1_x >> RESTBITS;
#else
        return bid_s0_s1_x;
#endif
    }
    __device__ u32 bucketid() const {
#ifdef XINTREE
        return bid_s0_s1_x >> (2 * SLOTBITS + RESTBITS);
#else
        return bid_s0_s1_x >> (2 * SLOTBITS);
#endif
    }
    __device__ u32 slotid0() const {
#ifdef XINTREE
        return (bid_s0_s1_x >> SLOTBITS + RESTBITS) & SLOTMASK;
#else
        return (bid_s0_s1_x >> SLOTBITS) & SLOTMASK;
#endif
    }
    __device__ u32 slotid1() const {
#ifdef XINTREE
        return (bid_s0_s1_x >> RESTBITS) & SLOTMASK;
#else
        return bid_s0_s1_x & SLOTMASK;
#endif
    }
    __device__ u32 xhash() const {
        return bid_s0_s1_x & RESTMASK;
    }
    __device__ bool prob_disjoint(const tree other) const {
        tree xort(bid_s0_s1_x ^ other.bid_s0_s1_x);
        return xort.bucketid() || (xort.slotid0() && xort.slotid1());
        // next two tests catch much fewer cases and are therefore skipped
        // && slotid0() != other.slotid1() && slotid1() != other.slotid0()
    }
    };

union hashunit {
    u32 word;
    uchar bytes[sizeof(u32)];
};

#define WORDS(bits)     ((bits + 31) / 32)
#define HASHWORDS0 WORDS(WN - DIGITBITS + RESTBITS)
#define HASHWORDS1 WORDS(WN - 2*DIGITBITS + RESTBITS)

struct slot0 {
    tree attr;
    hashunit hash[HASHWORDS0];
};

struct slot1 {
    tree attr;
    hashunit hash[HASHWORDS1];
};

// a bucket is NSLOTS treenodes
typedef slot0 bucket0[NSLOTS];
typedef slot1 bucket1[NSLOTS];
// the N-bit hash consists of K+1 n-bit "digits"
// each of which corresponds to a layer of NBUCKETS buckets
typedef bucket0 digit0[NBUCKETS];
typedef bucket1 digit1[NBUCKETS];

// size (in bytes) of hash in round 0 <= r < WK
u32 hhashsize(const u32 r) {
#ifdef XINTREE
    const u32 hashbits = WN - (r + 1) * DIGITBITS;
#else
    const u32 hashbits = WN - (r + 1) * DIGITBITS + RESTBITS;
#endif
    return (hashbits + 7) / 8;
}
// size (in bytes) of hash in round 0 <= r < WK
__device__ u32 hashsize(const u32 r) {
#ifdef XINTREE
    const u32 hashbits = WN - (r + 1) * DIGITBITS;
#else
    const u32 hashbits = WN - (r + 1) * DIGITBITS + RESTBITS;
#endif
    return (hashbits + 7) / 8;
}

u32 hhashwords(u32 bytes) {
    return (bytes + 3) / 4;
}

__device__ u32 hashwords(u32 bytes) {
    return (bytes + 3) / 4;
}

// manages hash and tree data
struct htalloc {
    bucket0 *trees0[(WK + 1) / 2];
    bucket1 *trees1[WK / 2];
};

typedef u32 bsizes[NBUCKETS];

struct equi {
    blake2b_state blake_ctx;
    htalloc hta;
    bsizes *nslots;
    proof *sols;
    u32 nsols;
    u32 nthreads;
    equi(const u32 n_threads) 
        : nsols(0)
    {
        nthreads = n_threads;
    }
    //void setheadernonce(const uint8_t *header, int len, const uint8_t* nonce, int nlen) {
    //    //setheader(&blake_ctx, header, len, nonce, nlen);
    //    nsols = 0;
    //}
    __device__ u32 getnslots0(const u32 bid) {
        u32 &nslot = nslots[0][bid];
        const u32 n = min(nslot, NSLOTS);
        nslot = 0;
        return n;
    }
    __device__ u32 getnslots1(const u32 bid) {
        u32 &nslot = nslots[1][bid];
        const u32 n = min(nslot, NSLOTS);
        nslot = 0;
        return n;
    }
    __device__ bool orderindices(u32 *indices, u32 size) {
        if (indices[0] > indices[size]) {
            for (u32 i = 0; i < size; i++) {
                const u32 tmp = indices[i];
                indices[i] = indices[size + i];
                indices[size + i] = tmp;
            }
        }
        return false;
    }
    __device__ bool listindices1(const tree t, u32 *indices) {
        const bucket0 &buck = hta.trees0[0][t.bucketid()];
        const u32 size = 1 << 0;
        indices[0] = buck[t.slotid0()].attr.getindex();
        indices[size] = buck[t.slotid1()].attr.getindex();
        orderindices(indices, size);
        return false;
    }
    __device__ bool listindices2(const tree t, u32 *indices) {
        const bucket1 &buck = hta.trees1[0][t.bucketid()];
        const u32 size = 1 << 1;
        return listindices1(buck[t.slotid0()].attr, indices) ||
            listindices1(buck[t.slotid1()].attr, indices + size) ||
            orderindices(indices, size) || indices[0] == indices[size];
    }
    __device__ bool listindices3(const tree t, u32 *indices) {
        const bucket0 &buck = hta.trees0[1][t.bucketid()];
        const u32 size = 1 << 2;
        return listindices2(buck[t.slotid0()].attr, indices) ||
            listindices2(buck[t.slotid1()].attr, indices + size) ||
            orderindices(indices, size) || indices[0] == indices[size];
    }
    __device__ bool listindices4(const tree t, u32 *indices) {
        const bucket1 &buck = hta.trees1[1][t.bucketid()];
        const u32 size = 1 << 3;
        return listindices3(buck[t.slotid0()].attr, indices) ||
            listindices3(buck[t.slotid1()].attr, indices + size) ||
            orderindices(indices, size) || indices[0] == indices[size];
    }
    __device__ bool listindices5(const tree t, u32 *indices) {
        const bucket0 &buck = hta.trees0[2][t.bucketid()];
        const u32 size = 1 << 4;
        return listindices4(buck[t.slotid0()].attr, indices) ||
            listindices4(buck[t.slotid1()].attr, indices + size) ||
            orderindices(indices, size) || indices[0] == indices[size];
    }

    __device__ bool listindices6(const tree t, u32 *indices) {
        const bucket1 &buck = hta.trees1[2][t.bucketid()];
        const u32 size = 1 << 5;
        return listindices5(buck[t.slotid0()].attr, indices) ||
            listindices5(buck[t.slotid1()].attr, indices + size) ||
            orderindices(indices, size) || indices[0] == indices[size];
    }
    __device__ bool listindices7(const tree t, u32 *indices) {
        const bucket0 &buck = hta.trees0[3][t.bucketid()];
        const u32 size = 1 << 6;
        return listindices6(buck[t.slotid0()].attr, indices) ||
            listindices6(buck[t.slotid1()].attr, indices + size) ||
            orderindices(indices, size) || indices[0] == indices[size];
    }
    __device__ bool listindices8(const tree t, u32 *indices) {
        const bucket1 &buck = hta.trees1[3][t.bucketid()];
        const u32 size = 1 << 7;
        return listindices7(buck[t.slotid0()].attr, indices) ||
            listindices7(buck[t.slotid1()].attr, indices + size) ||
            orderindices(indices, size) || indices[0] == indices[size];
    }
    __device__ bool listindices9(const tree t, u32 *indices) {
        const bucket0 &buck = hta.trees0[4][t.bucketid()];
        const u32 size = 1 << 8;
        return listindices8(buck[t.slotid0()].attr, indices) ||
            listindices8(buck[t.slotid1()].attr, indices + size) ||
            orderindices(indices, size) || indices[0] == indices[size];
    }
    __device__ void candidate(const tree t) {
        proof prf;
#if WK==9
        if (listindices9(t, prf)) return;
#elif WK==7
        if (listindices7(t, prf)) return;
#elif WK==5
        if (listindices5(t, prf)) return;
#else
#error not implemented
#endif
        u32 soli = atomicAdd(&nsols, 1);
        if (soli < MAXSOLS)
#if WK==9
            listindices9(t, sols[soli]);
#elif WK==7
            listindices7(t, sols[soli]);
#elif WK==5
            listindices5(t, sols[soli]);
#else
#error not implemented
#endif
    }
    void showbsizes(u32 r) {
#if defined(HIST) || defined(SPARK) || defined(LOGSPARK)
        u32 ns[NBUCKETS];
        checkCudaErrors(cudaMemcpy(ns, nslots[r & 1], NBUCKETS * sizeof(u32), cudaMemcpyDeviceToHost));
        u32 binsizes[65];
        memset(binsizes, 0, 65 * sizeof(u32));
        for (u32 bucketid = 0; bucketid < NBUCKETS; bucketid++) {
            u32 bsize = min(ns[bucketid], NSLOTS) >> (SLOTBITS - 6);
            binsizes[bsize]++;
        }
        for (u32 i = 0; i < 65; i++) {
#ifdef HIST
            printf(" %d:%d", i, binsizes[i]);
#else
#ifdef SPARK
            u32 sparks = binsizes[i] / SPARKSCALE;
#else
            u32 sparks = 0;
            for (u32 bs = binsizes[i]; bs; bs >>= 1) sparks++;
            sparks = sparks * 7 / SPARKSCALE;
#endif
            printf("\342\226%c", '\201' + sparks);
#endif
        }
        printf("\n");
#endif
    }
    struct htlayout {
        htalloc hta;
        u32 prevhashunits;
        u32 nexthashunits;
        u32 dunits;
        u32 prevbo;
        u32 nextbo;

        __device__ htlayout(equi *eq, u32 r) : hta(eq->hta), prevhashunits(0), dunits(0) {
            u32 nexthashbytes = hashsize(r);
            nexthashunits = hashwords(nexthashbytes);
            prevbo = 0;
            nextbo = nexthashunits * sizeof(hashunit) - nexthashbytes; // 0-3
            if (r) {
                u32 prevhashbytes = hashsize(r - 1);
                prevhashunits = hashwords(prevhashbytes);
                prevbo = prevhashunits * sizeof(hashunit) - prevhashbytes; // 0-3
                dunits = prevhashunits - nexthashunits;
            }
        }
        __device__ u32 getxhash0(const slot0* pslot) const {
#ifdef XINTREE
            return pslot->attr.xhash();
#elif DIGITBITS % 8 == 4 && RESTBITS == 4
            return pslot->hash->bytes[prevbo] >> 4;
#elif DIGITBITS % 8 == 4 && RESTBITS == 6
            return (pslot->hash->bytes[prevbo] & 0x3) << 4 | pslot->hash->bytes[prevbo + 1] >> 4;
#elif DIGITBITS % 8 == 4 && RESTBITS == 8
            return (pslot->hash->bytes[prevbo] & 0xf) << 4 | pslot->hash->bytes[prevbo + 1] >> 4;
#elif DIGITBITS % 8 == 4 && RESTBITS == 10
            return (pslot->hash->bytes[prevbo] & 0x3f) << 4 | pslot->hash->bytes[prevbo + 1] >> 4;
#elif DIGITBITS % 8 == 0 && RESTBITS == 4
            return pslot->hash->bytes[prevbo] & 0xf;
#elif RESTBITS == 0
            return 0;
#else
#error non implemented
#endif
        }
        __device__ u32 getxhash1(const slot1* pslot) const {
#ifdef XINTREE
            return pslot->attr.xhash();
#elif DIGITBITS % 4 == 0 && RESTBITS == 4
            return pslot->hash->bytes[prevbo] & 0xf;
#elif DIGITBITS % 4 == 0 && RESTBITS == 6
            return pslot->hash->bytes[prevbo] & 0x3f;
#elif DIGITBITS % 4 == 0 && RESTBITS == 8
            return pslot->hash->bytes[prevbo];
#elif DIGITBITS % 4 == 0 && RESTBITS == 10
            return (pslot->hash->bytes[prevbo] & 0x3) << 8 | pslot->hash->bytes[prevbo + 1];
#elif RESTBITS == 0
            return 0;
#else
#error non implemented
#endif
        }
        __device__ bool equal(const hashunit *hash0, const hashunit *hash1) const {
            return hash0[prevhashunits - 1].word == hash1[prevhashunits - 1].word;
        }
    };

    struct collisiondata {
#ifdef XBITMAP
#if NSLOTS > 64
#error cant use XBITMAP with more than 64 slots
#endif
        u64 xhashmap[NRESTS];
        u64 xmap;
#else
#if RESTBITS <= 6
        typedef uchar xslot;
#else
        typedef u16 xslot;
#endif
        static const xslot xnil = ~0;
        xslot xhashslots[NRESTS];
        xslot nextxhashslot[NSLOTS];
        xslot nextslot;
#endif
        u32 s0;

        __device__ void clear() {
#ifdef XBITMAP
            memset(xhashmap, 0, NRESTS * sizeof(u64));
#else
            memset(xhashslots, xnil, NRESTS * sizeof(xslot));
            memset(nextxhashslot, xnil, NSLOTS * sizeof(xslot));
#endif
        }
        __device__ void addslot(u32 s1, u32 xh) {
#ifdef XBITMAP
            xmap = xhashmap[xh];
            xhashmap[xh] |= (u64)1 << s1;
            s0 = ~0;
#else
            nextslot = xhashslots[xh];
            nextxhashslot[s1] = nextslot;
            xhashslots[xh] = s1;
#endif
        }
        __device__ bool nextcollision() const {
#ifdef XBITMAP
            return xmap != 0;
#else
            return nextslot != xnil;
#endif
        }
        __device__ u32 slot() {
#ifdef XBITMAP
            const u32 ffs = __ffsll(xmap);
            s0 += ffs; xmap >>= ffs;
#else
            nextslot = nextxhashslot[s0 = nextslot];
#endif
            return s0;
        }
    };
};

__global__ void digitH(equi *eq) {
    uchar hash[HASHOUT];
    blake2b_state state;
    equi::htlayout htl(eq, 0);
    const u32 hashbytes = hashsize(0);
    const u32 id = blockIdx.x * blockDim.x + threadIdx.x;
    for (u32 block = id; block < NBLOCKS; block += eq->nthreads) {
        state = eq->blake_ctx;
        blake2b_gpu_hash(&state, block, hash, HASHOUT);
        for (u32 i = 0; i < HASHESPERBLAKE; i++) {
            const uchar *ph = hash + i * WN / 8;
#if BUCKBITS == 16 && RESTBITS == 4
            const u32 bucketid = ((u32)ph[0] << 8) | ph[1];
#ifdef XINTREE
            const u32 xhash = ph[2] >> 4;
#endif
#elif BUCKBITS == 14 && RESTBITS == 6
            const u32 bucketid = ((u32)ph[0] << 6) | ph[1] >> 2;
#elif BUCKBITS == 12 && RESTBITS == 8
            const u32 bucketid = ((u32)ph[0] << 4) | ph[1] >> 4;
#elif BUCKBITS == 20 && RESTBITS == 4
            const u32 bucketid = ((((u32)ph[0] << 8) | ph[1]) << 4) | ph[2] >> 4;
#ifdef XINTREE
            const u32 xhash = ph[2] & 0xf;
#endif
#elif BUCKBITS == 12 && RESTBITS == 4
            const u32 bucketid = ((u32)ph[0] << 4) | ph[1] >> 4;
#ifdef XINTREE
            const u32 xhash = ph[1] & 0xf;
#endif
#else
#error not implemented
#endif
            const u32 slot = atomicAdd(&eq->nslots[0][bucketid], 1);
            if (slot >= NSLOTS)
                continue;
            slot0 &s = eq->hta.trees0[0][bucketid][slot];
#ifdef XINTREE
            s.attr = tree(block*HASHESPERBLAKE + i, xhash);
#else
            s.attr = tree(block*HASHESPERBLAKE + i);
#endif
            memcpy(s.hash->bytes + htl.nextbo, ph + WN / 8 - hashbytes, hashbytes);
        }
    }
}

__global__ void digitO(equi *eq, const u32 r) {
    equi::htlayout htl(eq, r);
    equi::collisiondata cd;
    const u32 id = blockIdx.x * blockDim.x + threadIdx.x;
    for (u32 bucketid = id; bucketid < NBUCKETS; bucketid += eq->nthreads) {
        cd.clear();
        slot0 *buck = htl.hta.trees0[(r - 1) / 2][bucketid];
        u32 bsize = eq->getnslots0(bucketid);
        for (u32 s1 = 0; s1 < bsize; s1++) {
            const slot0 *pslot1 = buck + s1;
            for (cd.addslot(s1, htl.getxhash0(pslot1)); cd.nextcollision(); ) {
                const u32 s0 = cd.slot();
                const slot0 *pslot0 = buck + s0;
                if (htl.equal(pslot0->hash, pslot1->hash))
                    continue;
                u32 xorbucketid;
                u32 xhash;
                const uchar *bytes0 = pslot0->hash->bytes, *bytes1 = pslot1->hash->bytes;
#if WN == 200 && BUCKBITS == 16 && RESTBITS == 4 && defined(XINTREE)
                xorbucketid = ((((u32)(bytes0[htl.prevbo] ^ bytes1[htl.prevbo]) & 0xf) << 8)
                    | (bytes0[htl.prevbo + 1] ^ bytes1[htl.prevbo + 1])) << 4
                    | (xhash = bytes0[htl.prevbo + 2] ^ bytes1[htl.prevbo + 2]) >> 4;
                xhash &= 0xf;
#elif WN % 24 == 0 && BUCKBITS == 20 && RESTBITS == 4
                xorbucketid = ((((u32)(bytes0[htl.prevbo + 1] ^ bytes1[htl.prevbo + 1]) << 8)
                    | (bytes0[htl.prevbo + 2] ^ bytes1[htl.prevbo + 2])) << 4)
                    | (xhash = bytes0[htl.prevbo + 3] ^ bytes1[htl.prevbo + 3]) >> 4;
                xhash &= 0xf;
#elif WN == 96 && BUCKBITS == 12 && RESTBITS == 4
                xorbucketid = ((u32)(bytes0[htl.prevbo + 1] ^ bytes1[htl.prevbo + 1]) << 4)
                    | (xhash = bytes0[htl.prevbo + 2] ^ bytes1[htl.prevbo + 2]) >> 4;
                xhash &= 0xf;
#elif WN == 200 && BUCKBITS == 14 && RESTBITS == 6
                xorbucketid = ((((u32)(bytes0[htl.prevbo + 1] ^ bytes1[htl.prevbo + 1]) & 0xf) << 8)
                    | (bytes0[htl.prevbo + 2] ^ bytes1[htl.prevbo + 2])) << 2
                    | (bytes0[htl.prevbo + 3] ^ bytes1[htl.prevbo + 3]) >> 6;
#else
#error not implemented
#endif
                const u32 xorslot = atomicAdd(&eq->nslots[1][xorbucketid], 1);
                if (xorslot >= NSLOTS)
                    continue;
                slot1 &xs = htl.hta.trees1[r / 2][xorbucketid][xorslot];
#ifdef XINTREE
                xs.attr = tree(bucketid, s0, s1, xhash);
#else
                xs.attr = tree(bucketid, s0, s1);
#endif
                for (u32 i = htl.dunits; i < htl.prevhashunits; i++)
                    xs.hash[i - htl.dunits].word = pslot0->hash[i].word ^ pslot1->hash[i].word;
            }
        }
    }
}

__global__ void digitE(equi *eq, const u32 r) {
    equi::htlayout htl(eq, r);
    equi::collisiondata cd;
    const u32 id = blockIdx.x * blockDim.x + threadIdx.x;
    for (u32 bucketid = id; bucketid < NBUCKETS; bucketid += eq->nthreads) {
        cd.clear();
        slot1 *buck = htl.hta.trees1[(r - 1) / 2][bucketid];
        u32 bsize = eq->getnslots1(bucketid);
        for (u32 s1 = 0; s1 < bsize; s1++) {
            const slot1 *pslot1 = buck + s1;
            for (cd.addslot(s1, htl.getxhash1(pslot1)); cd.nextcollision(); ) {
                const u32 s0 = cd.slot();
                const slot1 *pslot0 = buck + s0;
                if (htl.equal(pslot0->hash, pslot1->hash))
                    continue;
                u32 xorbucketid;
                const uchar *bytes0 = pslot0->hash->bytes, *bytes1 = pslot1->hash->bytes;
#if WN == 200 && BUCKBITS == 16 && RESTBITS == 4 && defined(XINTREE)
                xorbucketid = ((u32)(bytes0[htl.prevbo] ^ bytes1[htl.prevbo]) << 8)
                    | (bytes0[htl.prevbo + 1] ^ bytes1[htl.prevbo + 1]);
                u32 xhash = (bytes0[htl.prevbo + 2] ^ bytes1[htl.prevbo + 2]) >> 4;
#elif WN % 24 == 0 && BUCKBITS == 20 && RESTBITS == 4
                xorbucketid = ((((u32)(bytes0[htl.prevbo + 1] ^ bytes1[htl.prevbo + 1]) << 8)
                    | (bytes0[htl.prevbo + 2] ^ bytes1[htl.prevbo + 2])) << 4)
                    | (bytes0[htl.prevbo + 3] ^ bytes1[htl.prevbo + 3]) >> 4;
#elif WN == 96 && BUCKBITS == 12 && RESTBITS == 4
                xorbucketid = ((u32)(bytes0[htl.prevbo + 1] ^ bytes1[htl.prevbo + 1]) << 4)
                    | (bytes0[htl.prevbo + 2] ^ bytes1[htl.prevbo + 2]) >> 4;
#elif WN == 200 && BUCKBITS == 14 && RESTBITS == 6
                xorbucketid = ((u32)(bytes0[htl.prevbo + 1] ^ bytes1[htl.prevbo + 1]) << 6)
                    | (bytes0[htl.prevbo + 2] ^ bytes1[htl.prevbo + 2]) >> 2;
#else
#error not implemented
#endif
                const u32 xorslot = atomicAdd(&eq->nslots[0][xorbucketid], 1);
                if (xorslot >= NSLOTS)
                    continue;
                slot0 &xs = htl.hta.trees0[r / 2][xorbucketid][xorslot];
#ifdef XINTREE
                xs.attr = tree(bucketid, s0, s1, xhash);
#else
                xs.attr = tree(bucketid, s0, s1);
#endif
                for (u32 i = htl.dunits; i < htl.prevhashunits; i++)
                    xs.hash[i - htl.dunits].word = pslot0->hash[i].word ^ pslot1->hash[i].word;
            }
        }
    }
}

#ifdef UNROLL
// bucket mask
static const u32 BUCKMASK = NBUCKETS - 1;

__global__ void digit_1(equi *eq) {
    equi::htlayout htl(eq, 1);
    equi::collisiondata cd;
    const u32 id = blockIdx.x * blockDim.x + threadIdx.x;
    for (u32 bucketid = id; bucketid < NBUCKETS; bucketid += eq->nthreads) {
        cd.clear();
        slot0 *buck = htl.hta.trees0[0][bucketid];
        u32 bsize = eq->getnslots0(bucketid);
        for (u32 s1 = 0; s1 < bsize; s1++) {
            const slot0 *pslot1 = buck + s1;
            for (cd.addslot(s1, htl.getxhash0(pslot1)); cd.nextcollision(); ) {
                const u32 s0 = cd.slot();
                const slot0 *pslot0 = buck + s0;
                if (htl.equal(pslot0->hash, pslot1->hash))
                    continue;
                const u32 xor0 = pslot0->hash->word ^ pslot1->hash->word;
                const u32 bexor = __byte_perm(xor0, 0, 0x0123);
                const u32 xorbucketid = bexor >> 4 & BUCKMASK;
                const u32 xhash = bexor & 0xf;
                const u32 xorslot = atomicAdd(&eq->nslots[1][xorbucketid], 1);
                if (xorslot >= NSLOTS)
                    continue;
                slot1 &xs = htl.hta.trees1[0][xorbucketid][xorslot];
                xs.attr = tree(bucketid, s0, s1, xhash);
                xs.hash[0].word = pslot0->hash[1].word ^ pslot1->hash[1].word;
                xs.hash[1].word = pslot0->hash[2].word ^ pslot1->hash[2].word;
                xs.hash[2].word = pslot0->hash[3].word ^ pslot1->hash[3].word;
                xs.hash[3].word = pslot0->hash[4].word ^ pslot1->hash[4].word;
                xs.hash[4].word = pslot0->hash[5].word ^ pslot1->hash[5].word;
            }
        }
    }
}
__global__ void digit2(equi *eq) {
    equi::htlayout htl(eq, 2);
    equi::collisiondata cd;
    const u32 id = blockIdx.x * blockDim.x + threadIdx.x;
    for (u32 bucketid = id; bucketid < NBUCKETS; bucketid += eq->nthreads) {
        cd.clear();
        slot1 *buck = htl.hta.trees1[0][bucketid];
        u32 bsize = eq->getnslots1(bucketid);
        for (u32 s1 = 0; s1 < bsize; s1++) {
            const slot1 *pslot1 = buck + s1;
            for (cd.addslot(s1, htl.getxhash1(pslot1)); cd.nextcollision(); ) {
                const u32 s0 = cd.slot();
                const slot1 *pslot0 = buck + s0;
                if (htl.equal(pslot0->hash, pslot1->hash))
                    continue;
                const u32 xor0 = pslot0->hash->word ^ pslot1->hash->word;
                const u32 bexor = __byte_perm(xor0, 0, 0x0123);
                const u32 xorbucketid = bexor >> 16;
                const u32 xhash = bexor >> 12 & 0xf;
                const u32 xorslot = atomicAdd(&eq->nslots[0][xorbucketid], 1);
                if (xorslot >= NSLOTS)
                    continue;
                slot0 &xs = htl.hta.trees0[1][xorbucketid][xorslot];
                xs.attr = tree(bucketid, s0, s1, xhash);
                xs.hash[0].word = xor0;
                xs.hash[1].word = pslot0->hash[1].word ^ pslot1->hash[1].word;
                xs.hash[2].word = pslot0->hash[2].word ^ pslot1->hash[2].word;
                xs.hash[3].word = pslot0->hash[3].word ^ pslot1->hash[3].word;
                xs.hash[4].word = pslot0->hash[4].word ^ pslot1->hash[4].word;
            }
        }
    }
}
__global__ void digit3(equi *eq) {
    equi::htlayout htl(eq, 3);
    equi::collisiondata cd;
    const u32 id = blockIdx.x * blockDim.x + threadIdx.x;
    for (u32 bucketid = id; bucketid < NBUCKETS; bucketid += eq->nthreads) {
        cd.clear();
        slot0 *buck = htl.hta.trees0[1][bucketid];
        u32 bsize = eq->getnslots0(bucketid);
        for (u32 s1 = 0; s1 < bsize; s1++) {
            const slot0 *pslot1 = buck + s1;
            for (cd.addslot(s1, htl.getxhash0(pslot1)); cd.nextcollision(); ) {
                const u32 s0 = cd.slot();
                const slot0 *pslot0 = buck + s0;
                if (htl.equal(pslot0->hash, pslot1->hash))
                    continue;
                const u32 xor0 = pslot0->hash->word ^ pslot1->hash->word;
                const u32 xor1 = pslot0->hash[1].word ^ pslot1->hash[1].word;
                const u32 bexor = __byte_perm(xor0, xor1, 0x1234);
                const u32 xorbucketid = bexor >> 4 & BUCKMASK;
                const u32 xhash = bexor & 0xf;
                const u32 xorslot = atomicAdd(&eq->nslots[1][xorbucketid], 1);
                if (xorslot >= NSLOTS)
                    continue;
                slot1 &xs = htl.hta.trees1[1][xorbucketid][xorslot];
                xs.attr = tree(bucketid, s0, s1, xhash);
                xs.hash[0].word = xor1;
                xs.hash[1].word = pslot0->hash[2].word ^ pslot1->hash[2].word;
                xs.hash[2].word = pslot0->hash[3].word ^ pslot1->hash[3].word;
                xs.hash[3].word = pslot0->hash[4].word ^ pslot1->hash[4].word;
            }
        }
    }
}
__global__ void digit4(equi *eq) {
    equi::htlayout htl(eq, 4);
    equi::collisiondata cd;
    const u32 id = blockIdx.x * blockDim.x + threadIdx.x;
    for (u32 bucketid = id; bucketid < NBUCKETS; bucketid += eq->nthreads) {
        cd.clear();
        slot1 *buck = htl.hta.trees1[1][bucketid];
        u32 bsize = eq->getnslots1(bucketid);
        for (u32 s1 = 0; s1 < bsize; s1++) {
            const slot1 *pslot1 = buck + s1;
            for (cd.addslot(s1, htl.getxhash1(pslot1)); cd.nextcollision(); ) {
                const u32 s0 = cd.slot();
                const slot1 *pslot0 = buck + s0;
                if (htl.equal(pslot0->hash, pslot1->hash))
                    continue;
                const u32 xor0 = pslot0->hash->word ^ pslot1->hash->word;
                const u32 bexor = __byte_perm(xor0, 0, 0x4123);
                const u32 xorbucketid = bexor >> 8;
                const u32 xhash = bexor >> 4 & 0xf;
                const u32 xorslot = atomicAdd(&eq->nslots[0][xorbucketid], 1);
                if (xorslot >= NSLOTS)
                    continue;
                slot0 &xs = htl.hta.trees0[2][xorbucketid][xorslot];
                xs.attr = tree(bucketid, s0, s1, xhash);
                xs.hash[0].word = xor0;
                xs.hash[1].word = pslot0->hash[1].word ^ pslot1->hash[1].word;
                xs.hash[2].word = pslot0->hash[2].word ^ pslot1->hash[2].word;
                xs.hash[3].word = pslot0->hash[3].word ^ pslot1->hash[3].word;
            }
        }
    }
}
__global__ void digit5(equi *eq) {
    equi::htlayout htl(eq, 5);
    equi::collisiondata cd;
    const u32 id = blockIdx.x * blockDim.x + threadIdx.x;
    for (u32 bucketid = id; bucketid < NBUCKETS; bucketid += eq->nthreads) {
        cd.clear();
        slot0 *buck = htl.hta.trees0[2][bucketid];
        u32 bsize = eq->getnslots0(bucketid);
        for (u32 s1 = 0; s1 < bsize; s1++) {
            const slot0 *pslot1 = buck + s1;
            for (cd.addslot(s1, htl.getxhash0(pslot1)); cd.nextcollision(); ) {
                const u32 s0 = cd.slot();
                const slot0 *pslot0 = buck + s0;
                if (htl.equal(pslot0->hash, pslot1->hash))
                    continue;
                const u32 xor0 = pslot0->hash->word ^ pslot1->hash->word;
                const u32 xor1 = pslot0->hash[1].word ^ pslot1->hash[1].word;
                const u32 bexor = __byte_perm(xor0, xor1, 0x2345);
                const u32 xorbucketid = bexor >> 4 & BUCKMASK;
                const u32 xhash = bexor & 0xf;
                const u32 xorslot = atomicAdd(&eq->nslots[1][xorbucketid], 1);
                if (xorslot >= NSLOTS)
                    continue;
                slot1 &xs = htl.hta.trees1[2][xorbucketid][xorslot];
                xs.attr = tree(bucketid, s0, s1, xhash);
                xs.hash[0].word = xor1;
                xs.hash[1].word = pslot0->hash[2].word ^ pslot1->hash[2].word;
                xs.hash[2].word = pslot0->hash[3].word ^ pslot1->hash[3].word;
            }
        }
    }
}
__global__ void digit6(equi *eq) {
    equi::htlayout htl(eq, 6);
    equi::collisiondata cd;
    const u32 id = blockIdx.x * blockDim.x + threadIdx.x;
    for (u32 bucketid = id; bucketid < NBUCKETS; bucketid += eq->nthreads) {
        cd.clear();
        slot1 *buck = htl.hta.trees1[2][bucketid];
        u32 bsize = eq->getnslots1(bucketid);
        for (u32 s1 = 0; s1 < bsize; s1++) {
            const slot1 *pslot1 = buck + s1;
            for (cd.addslot(s1, htl.getxhash1(pslot1)); cd.nextcollision(); ) {
                const u32 s0 = cd.slot();
                const slot1 *pslot0 = buck + s0;
                if (htl.equal(pslot0->hash, pslot1->hash))
                    continue;
                const u32 xor0 = pslot0->hash->word ^ pslot1->hash->word;
                const u32 xor1 = pslot0->hash[1].word ^ pslot1->hash[1].word;
                const u32 bexor = __byte_perm(xor0, xor1, 0x2345);
                const u32 xorbucketid = bexor >> 16;
                const u32 xhash = bexor >> 12 & 0xf;
                const u32 xorslot = atomicAdd(&eq->nslots[0][xorbucketid], 1);
                if (xorslot >= NSLOTS)
                    continue;
                slot0 &xs = htl.hta.trees0[3][xorbucketid][xorslot];
                xs.attr = tree(bucketid, s0, s1, xhash);
                xs.hash[0].word = xor1;
                xs.hash[1].word = pslot0->hash[2].word ^ pslot1->hash[2].word;
            }
        }
    }
}
__global__ void digit7(equi *eq) {
    equi::htlayout htl(eq, 7);
    equi::collisiondata cd;
    const u32 id = blockIdx.x * blockDim.x + threadIdx.x;
    for (u32 bucketid = id; bucketid < NBUCKETS; bucketid += eq->nthreads) {
        cd.clear();
        slot0 *buck = htl.hta.trees0[3][bucketid];
        u32 bsize = eq->getnslots0(bucketid);
        for (u32 s1 = 0; s1 < bsize; s1++) {
            const slot0 *pslot1 = buck + s1;
            for (cd.addslot(s1, htl.getxhash0(pslot1)); cd.nextcollision(); ) {
                const u32 s0 = cd.slot();
                const slot0 *pslot0 = buck + s0;
                if (htl.equal(pslot0->hash, pslot1->hash))
                    continue;
                const u32 xor0 = pslot0->hash->word ^ pslot1->hash->word;
                const u32 bexor = __byte_perm(xor0, 0, 0x4012);
                const u32 xorbucketid = bexor >> 4 & BUCKMASK;
                const u32 xhash = bexor & 0xf;
                const u32 xorslot = atomicAdd(&eq->nslots[1][xorbucketid], 1);
                if (xorslot >= NSLOTS)
                    continue;
                slot1 &xs = htl.hta.trees1[3][xorbucketid][xorslot];
                xs.attr = tree(bucketid, s0, s1, xhash);
                xs.hash[0].word = xor0;
                xs.hash[1].word = pslot0->hash[1].word ^ pslot1->hash[1].word;
            }
        }
    }
}
__global__ void digit8(equi *eq) {
    equi::htlayout htl(eq, 8);
    equi::collisiondata cd;
    const u32 id = blockIdx.x * blockDim.x + threadIdx.x;
    for (u32 bucketid = id; bucketid < NBUCKETS; bucketid += eq->nthreads) {
        cd.clear();
        slot1 *buck = htl.hta.trees1[3][bucketid];
        u32 bsize = eq->getnslots1(bucketid);
        for (u32 s1 = 0; s1 < bsize; s1++) {
            const slot1 *pslot1 = buck + s1;
            for (cd.addslot(s1, htl.getxhash1(pslot1)); cd.nextcollision(); ) {
                const u32 s0 = cd.slot();
                const slot1 *pslot0 = buck + s0;
                if (htl.equal(pslot0->hash, pslot1->hash))
                    continue;
                const u32 xor0 = pslot0->hash->word ^ pslot1->hash->word;
                const u32 xor1 = pslot0->hash[1].word ^ pslot1->hash[1].word;
                const u32 bexor = __byte_perm(xor0, xor1, 0x3456);
                const u32 xorbucketid = bexor >> 16;
                const u32 xhash = bexor >> 12 & 0xf;
                const u32 xorslot = atomicAdd(&eq->nslots[0][xorbucketid], 1);
                if (xorslot >= NSLOTS)
                    continue;
                slot0 &xs = htl.hta.trees0[4][xorbucketid][xorslot];
                xs.attr = tree(bucketid, s0, s1, xhash);
                xs.hash[0].word = xor1;
            }
        }
    }
}
#endif

__global__ void digitK(equi *eq) {
    equi::collisiondata cd;
    equi::htlayout htl(eq, WK);
    const u32 id = blockIdx.x * blockDim.x + threadIdx.x;
    for (u32 bucketid = id; bucketid < NBUCKETS; bucketid += eq->nthreads) {
        cd.clear();
        slot0 *buck = htl.hta.trees0[(WK - 1) / 2][bucketid];
        u32 bsize = eq->getnslots0(bucketid); // assume WK odd
        for (u32 s1 = 0; s1 < bsize; s1++) {
            const slot0 *pslot1 = buck + s1;
            for (cd.addslot(s1, htl.getxhash0(pslot1)); cd.nextcollision(); ) { // assume WK odd
                const u32 s0 = cd.slot();
                const slot0 *pslot0 = buck + s0;
                if (htl.equal(pslot0->hash, pslot1->hash) && pslot0->attr.prob_disjoint(pslot1->attr)) {
#ifdef XINTREE
                    eq->candidate(tree(bucketid, s0, s1, 0));
#else
                    eq->candidate(tree(bucketid, s0, s1));
#endif
                }
            }
        }
    }
}

static void compress_solution(const uint32_t* sol, uint8_t* csol)
{
    uint8_t b;

    for (int i = 0, j = 0, bits_left = DIGITBITS + 1;
        j < COMPRESSED_SOL_SIZE; csol[j++] = b) {
        if (bits_left >= 8) {
            // Read next 8 bits, stay at same sol index
            b = sol[i] >> (bits_left -= 8);
        }
        else { // less than 8 bits to read
       // Read remaining bits and shift left to make space for next sol index
            b = sol[i];
            b <<= (8 - bits_left); // may also set b=0 if bits_left was 0, which is fine
            // Go to next sol index and read remaining bits
            bits_left += DIGITBITS + 1 - 8;
            b |= sol[++i] >> bits_left;
        }
    }
}

struct GpuContext
{
    GpuContext(const blake2b_state& state)
        : threadsperblock(64)/// TODO: get it somewhere
        , totalblocks(20 * 7)/// TODO: get it somewhere
        , device_id(0)
        , eq(new equi(threadsperblock * totalblocks))
        , solutionBuf(sizeof(proof) * MAXSOLS + 4096)
    {
        eq->blake_ctx = state;

        // TODO: ugly code, must be rewrited
        {
            solutions = (uint32_t*)(((long long)solutionBuf.data() + 4095) & -4096);
        }

        checkCudaErrors(cudaSetDevice(device_id));
        checkCudaErrors(cudaDeviceReset());
        checkCudaErrors(cudaSetDeviceFlags(cudaDeviceScheduleBlockingSync));
        checkCudaErrors(cudaDeviceSetCacheConfig(cudaFuncCachePreferL1));

        checkCudaErrors(cudaMalloc((void**)&heap0, sizeof(digit0)));
        checkCudaErrors(cudaMalloc((void**)&heap1, sizeof(digit1)));

        for (u32 r = 0; r < WK; r++)
            if ((r & 1) == 0)
                eq->hta.trees0[r / 2] = (bucket0 *)(heap0 + r / 2);
            else
                eq->hta.trees1[r / 2] = (bucket1 *)(heap1 + r / 2);

        checkCudaErrors(cudaMalloc((void**)&eq->nslots, 2 * NBUCKETS * sizeof(u32)));
        checkCudaErrors(cudaMalloc((void**)&eq->sols, MAXSOLS * sizeof(proof)));

        checkCudaErrors(cudaMalloc((void**)&device_eq, sizeof(equi)));
    }

    ~GpuContext()
    {
        checkCudaErrors(cudaSetDevice(device_id));
        checkCudaErrors(cudaDeviceReset());
    }

    bool solve(const EquihashGpu::IsValid& valid, const EquihashGpu::Cancel& cancel)
    {
        checkCudaErrors(cudaSetDevice(device_id));
        checkCudaErrors(cudaMemcpy(device_eq, eq.get(), sizeof(equi), cudaMemcpyHostToDevice));

        digitH << <totalblocks, threadsperblock >> > (device_eq);

        if (cancel()) return true;

#if BUCKBITS == 16 && RESTBITS == 4 && defined XINTREE && defined(UNROLL)
        digit_1 << <totalblocks, threadsperblock >> > (device_eq);
        if (cancelf()) return;
        digit2 << <totalblocks, threadsperblock >> > (device_eq);
        if (cancelf()) return;
        digit3 << <totalblocks, threadsperblock >> > (device_eq);
        if (cancelf()) return;
        digit4 << <totalblocks, threadsperblock >> > (device_eq);
        if (cancelf()) return;
        digit5 << <totalblocks, threadsperblock >> > (device_eq);
        if (cancelf()) return;
        digit6 << <totalblocks, threadsperblock >> > (device_eq);
        if (cancelf()) return;
        digit7 << <totalblocks, threadsperblock >> > (device_eq);
        if (cancelf()) return;
        digit8 << <totalblocks, threadsperblock >> > (device_eq);
#else
        for (u32 r = 1; r < WK; r++) {
            r & 1 ? digitO << <totalblocks, threadsperblock >> > (device_eq, r)
                : digitE << <totalblocks, threadsperblock >> > (device_eq, r);
        }
#endif
        if (cancel()) return true;

        digitK << <totalblocks, threadsperblock >> > (device_eq);

        checkCudaErrors(cudaMemcpy(eq.get(), device_eq, sizeof(equi), cudaMemcpyDeviceToHost));
        checkCudaErrors(cudaMemcpy(solutions, eq->sols, MAXSOLS * sizeof(proof), cudaMemcpyDeviceToHost));

        for (unsigned s = 0; (s < eq->nsols) && (s < MAXSOLS); s++)
        {
            std::vector<uint32_t> index_vector(PROOFSIZE);
            for (u32 i = 0; i < PROOFSIZE; i++) {
                index_vector[i] = *(solutions + s * PROOFSIZE + i);
            }

            std::vector<uint8_t> compressedSol(COMPRESSED_SOL_SIZE);

            compress_solution(&index_vector[0], &compressedSol[0]);

            if (valid(compressedSol))
                return true;

            if (cancel()) return true;
        }

        return false;
    }

    int threadsperblock;
    int totalblocks;
    int device_id;
    uint32_t* heap0;
    uint32_t* heap1;
    std::unique_ptr<equi> eq;
    equi* device_eq;

    std::vector<uint8_t> solutionBuf;
    uint32_t* solutions;
};

}

bool EquihashGpu::solve(const blake2b_state& state, const IsValid& valid, const Cancel& cancel)
{
    return GpuContext(state).solve(valid, cancel);
}
