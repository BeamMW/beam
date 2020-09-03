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
	macro(dst, p) \
	macro(src, p) \
	macro(size, f2)

#define BVMOp_mov1(macro) \
	macro(dst, p) \
	macro(src, f1)

#define BVMOp_mov2(macro) \
	macro(dst, p) \
	macro(src, f2)

#define BVMOp_mov4(macro) \
	macro(dst, p) \
	macro(src, f4)

#define BVMOp_mov8(macro) \
	macro(dst, p) \
	macro(src, f8)

#define BVMOp_xor(macro) BVMOp_mov(macro)
#define BVMOp_xor1(macro) BVMOp_mov1(macro)
#define BVMOp_xor2(macro) BVMOp_mov2(macro)
#define BVMOp_xor4(macro) BVMOp_mov4(macro)
#define BVMOp_xor8(macro) BVMOp_mov8(macro)

#define BVMOp_cmp(macro) \
	macro(p1, p) \
	macro(p2, p) \
	macro(size, f2)

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
	macro(addr, f2) \
	macro(frame, f2) \

#define BVMOp_jmp(macro) \
	macro(addr, f2) \

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
	macro(dst, p) \
	macro(nDst, f2) \
	macro(key, p) \
	macro(nKey, f2)

#define BVMOp_save_var(macro) \
	macro(dst, p) \
	macro(nDst, f2) \
	macro(key, p) \
	macro(nKey, f2)

#define BVMOp_del_var(macro) \
	macro(key, p) \
	macro(nKey, f2)



#define BVM_OpCodes(macro) \
	macro(0x01, mov) \
	macro(0x02, mov1) \
	macro(0x03, mov2) \
	macro(0x04, mov4) \
	macro(0x05, mov8) \
	macro(0x09, xor) \
	macro(0x0a, xor1) \
	macro(0x0b, xor2) \
	macro(0x0c, xor4) \
	macro(0x0d, xor8) \
	macro(0x11, cmp) \
	macro(0x12, cmp1) \
	macro(0x13, cmp2) \
	macro(0x14, cmp4) \
	macro(0x15, cmp8) \
	macro(0x31, load_var) \
	macro(0x32, save_var) \
	macro(0x33, del_var) \
	macro(0x20, call) \
	macro(0x21, jmp) \
	macro(0x22, jz) \
	macro(0x23, jnz) \
	macro(0x24, jg) \
	macro(0x25, jb) \
	macro(0x26, jgz) \
	macro(0x27, jbz) \
	macro(0x38, fail) \
	macro(0x39, ret) \
	macro(0x3a, add_sig)

