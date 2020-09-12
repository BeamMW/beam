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

#define BVMOp_mov(macro) \
	macro(pDst, w,x) \
	macro(pSrc, r,x) \

#define BVMOp_xor(macro) BVMOp_mov(macro)
#define BVMOp_or(macro) BVMOp_mov(macro)
#define BVMOp_and(macro) BVMOp_mov(macro)
#define BVMOp_add(macro) BVMOp_mov(macro)
#define BVMOp_sub(macro) BVMOp_mov(macro)

#define BVMOp_inv(macro) \
	macro(pDst, w,x) \

#define BVMOp_inc(macro) BVMOp_inv(macro)
#define BVMOp_neg(macro) BVMOp_inv(macro)

#define BVMOp_mul(macro) \
	macro(pDst, w,x) \
	macro(pSrc1, r,x) \
	macro(pSrc2, r,x) \

#define BVMOp_div(macro) BVMOp_mul(macro)

#define BVMOp_cmp(macro) \
	macro(p1, r,x) \
	macro(p2, r,x) \

#define BVMOp_call(macro) \
	macro(nAddr, r,2) \
	macro(nFrame, r,2) \

#define BVMOp_call_far(macro) \
	macro(cid, r,32) \
	macro(iMethod, r,2) \
	macro(nFrame, r,2) \

#define BVMOp_getsp(macro) \
	macro(nRes, w,2) \

#define BVMOp_jmp(macro) \
	macro(nAddr, r,2) \

#define BVMOp_jz(macro) BVMOp_jmp(macro)
#define BVMOp_jnz(macro) BVMOp_jmp(macro)
#define BVMOp_jg(macro) BVMOp_jmp(macro)
#define BVMOp_jb(macro) BVMOp_jmp(macro)
#define BVMOp_jgz(macro) BVMOp_jmp(macro)
#define BVMOp_jbz(macro) BVMOp_jmp(macro)

#define BVMOp_fail(macro)
#define BVMOp_ret(macro)

#define BVMOp_add_sig(macro) \
	macro(pk, r,33)

#define BVMOp_load_var(macro) \
	macro(pDst, w,v) \
	macro(nDst, w,2) \
	macro(pKey, r,v) \
	macro(nKey, r,2)

#define BVMOp_save_var(macro) \
	macro(pDst, r,v) \
	macro(nDst, r,2) \
	macro(pKey, r,v) \
	macro(nKey, r,2)

#define BVMOp_funds_lock(macro) \
	macro(nAmount, r,8) \
	macro(nAssetID, r,4) \

#define BVMOp_funds_unlock(macro) BVMOp_funds_lock(macro)

#define BVMOp_ref_add(macro) \
	macro(cid, r,32)

#define BVMOp_ref_release(macro) BVMOp_ref_add(macro)

#define BVMOp_asset_create(macro) \
	macro(nAid, w,4) \
	macro(pMetaData, r,v) \
	macro(nMetaData, r,2)

#define BVMOp_asset_emit(macro) \
	macro(nAid, r,4) \
	macro(nAmount, r,8) \
	macro(bEmit, r,1)

#define BVMOp_asset_destroy(macro) \
	macro(nAid, r,4)

#define BVMOp_sort(macro) \
	macro(pArray, w,v) \
	macro(nCount, r,2) \
	macro(nElementWidth, r,2) \
	macro(nKeyPos, r,2) \
	macro(nKeyWidth, r,2) \

#define BVM_OpCodes(macro) \
	macro(mov) \
	macro(xor) \
	macro(or) \
	macro(and) \
	macro(cmp) \
	macro(add) \
	macro(sub) \
	macro(inv) \
	macro(inc) \
	macro(neg) \
	macro(mul) \
	macro(div) \
	macro(load_var) \
	macro(save_var) \
	macro(call) \
	macro(getsp) \
	macro(jmp) \
	macro(jz) \
	macro(jnz) \
	macro(jg) \
	macro(jb) \
	macro(jgz) \
	macro(jbz) \
	macro(fail) \
	macro(ret) \
	macro(call_far) \
	macro(add_sig) \
	macro(funds_lock) \
	macro(funds_unlock) \
	macro(ref_add) \
	macro(ref_release) \
	macro(asset_create) \
	macro(asset_emit) \
	macro(asset_destroy) \
	macro(sort) \

