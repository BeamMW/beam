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
	macro(pDst, p) \
	macro(pSrc, p) \
	macro(nSize, f2)

#define BVMOp_mov1(macro) \
	macro(pDst, p) \
	macro(nSrc, f1)

#define BVMOp_mov2(macro) \
	macro(pDst, p) \
	macro(nSrc, f2)

#define BVMOp_mov4(macro) \
	macro(pDst, p) \
	macro(nSrc, f4)

#define BVMOp_mov8(macro) \
	macro(pDst, p) \
	macro(nSrc, f8)

#define BVMOp_xor(macro) BVMOp_mov(macro)
#define BVMOp_xor1(macro) BVMOp_mov1(macro)
#define BVMOp_xor2(macro) BVMOp_mov2(macro)
#define BVMOp_xor4(macro) BVMOp_mov4(macro)
#define BVMOp_xor8(macro) BVMOp_mov8(macro)

#define BVMOp_or(macro) BVMOp_mov(macro)
#define BVMOp_or1(macro) BVMOp_mov1(macro)
#define BVMOp_or2(macro) BVMOp_mov2(macro)
#define BVMOp_or4(macro) BVMOp_mov4(macro)
#define BVMOp_or8(macro) BVMOp_mov8(macro)

#define BVMOp_and(macro) BVMOp_mov(macro)
#define BVMOp_and1(macro) BVMOp_mov1(macro)
#define BVMOp_and2(macro) BVMOp_mov2(macro)
#define BVMOp_and4(macro) BVMOp_mov4(macro)
#define BVMOp_and8(macro) BVMOp_mov8(macro)

#define BVMOp_add(macro) BVMOp_mov(macro)
#define BVMOp_add1(macro) BVMOp_mov1(macro)
#define BVMOp_add2(macro) BVMOp_mov2(macro)
#define BVMOp_add4(macro) BVMOp_mov4(macro)
#define BVMOp_add8(macro) BVMOp_mov8(macro)

#define BVMOp_sub(macro) BVMOp_mov(macro)
#define BVMOp_sub1(macro) BVMOp_mov1(macro)
#define BVMOp_sub2(macro) BVMOp_mov2(macro)
#define BVMOp_sub4(macro) BVMOp_mov4(macro)
#define BVMOp_sub8(macro) BVMOp_mov8(macro)

#define BVMOp_mul_ex(macro) \
	macro(pDst, p) \
	macro(nDst, f2) \
	macro(pSrc1, p) \
	macro(nSrc1, f2) \
	macro(pSrc2, p) \
	macro(nSrc2, f2) \

#define BVMOp_div_ex(macro) BVMOp_mul_ex(macro)

#define BVMOp_cmp(macro) \
	macro(p1, p) \
	macro(p2, p) \
	macro(nSize, f2)

#define BVMOp_cmp1(macro) \
	macro(a, f1) \
	macro(b, f1)

#define BVMOp_cmp2(macro) \
	macro(a, f2) \
	macro(b, f2)

#define BVMOp_cmp4(macro) \
	macro(a, f4) \
	macro(b, f4)

#define BVMOp_cmp8(macro) \
	macro(a, f8) \
	macro(b, f8)

#define BVMOp_call(macro) \
	macro(nAddr, f2) \
	macro(nFrame, f2) \

#define BVMOp_call_far(macro) \
	macro(pContractID, p) \
	macro(iMethod, f2) \
	macro(nFrame, f2) \

#define BVMOp_getsp(macro) \
	macro(pRes, p) \

#define BVMOp_jmp(macro) \
	macro(nAddr, f2) \

#define BVMOp_jz(macro) BVMOp_jmp(macro)
#define BVMOp_jnz(macro) BVMOp_jmp(macro)
#define BVMOp_jg(macro) BVMOp_jmp(macro)
#define BVMOp_jb(macro) BVMOp_jmp(macro)
#define BVMOp_jgz(macro) BVMOp_jmp(macro)
#define BVMOp_jbz(macro) BVMOp_jmp(macro)

#define BVMOp_fail(macro)
#define BVMOp_ret(macro)

#define BVMOp_add_sig(macro) \
	macro(pPubKey, p)

#define BVMOp_load_var(macro) \
	macro(pDst, p) \
	macro(pnDst, p) \
	macro(pKey, p) \
	macro(nKey, f2)

#define BVMOp_save_var(macro) \
	macro(pDst, p) \
	macro(nDst, f2) \
	macro(pKey, p) \
	macro(nKey, f2)

#define BVMOp_funds_lock(macro) \
	macro(nAmount, f8) \
	macro(nAssetID, f4) \

#define BVMOp_funds_unlock(macro) BVMOp_funds_lock(macro)

#define BVMOp_ref_add(macro) \
	macro(pContractID, p)

#define BVMOp_ref_release(macro) BVMOp_ref_add(macro)

#define BVMOp_asset_create(macro) \
	macro(pAid, p) \
	macro(pMetaData, p) \
	macro(nMetaData, f2)

#define BVMOp_asset_emit(macro) \
	macro(nAid, f4) \
	macro(nAmount, f8) \
	macro(bEmit, f1)

#define BVMOp_asset_destroy(macro) \
	macro(nAid, f4)

#define BVMOp_sort(macro) \
	macro(pArray, p) \
	macro(nCount, f2) \
	macro(nElementWidth, f2) \
	macro(nKeyPos, f2) \
	macro(nKeyWidth, f2) \

#define BVM_OpCodes(macro) \
	macro(mov) \
	macro(mov1) \
	macro(mov2) \
	macro(mov4) \
	macro(mov8) \
	macro(xor) \
	macro(xor1) \
	macro(xor2) \
	macro(xor4) \
	macro(xor8) \
	macro(or) \
	macro(or1) \
	macro(or2) \
	macro(or4) \
	macro(or8) \
	macro(and) \
	macro(and1) \
	macro(and2) \
	macro(and4) \
	macro(and8) \
	macro(cmp) \
	macro(cmp1) \
	macro(cmp2) \
	macro(cmp4) \
	macro(cmp8) \
	macro(add) \
	macro(add1) \
	macro(add2) \
	macro(add4) \
	macro(add8) \
	macro(sub) \
	macro(sub1) \
	macro(sub2) \
	macro(sub4) \
	macro(sub8) \
	macro(mul_ex) \
	macro(div_ex) \
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

