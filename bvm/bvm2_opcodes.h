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

#define BVMOp_Memcpy(macro, sep) \
	macro(void*, pDst) sep \
	macro(const void*, pSrc) sep \
	macro(uint32_t, size)

#define BVMOp_Memcmp(macro, sep) \
	macro(const void*, p1) sep \
	macro(const void*, p2) sep \
	macro(uint32_t, size)

#define BVMOp_Memis0(macro, sep) \
	macro(const void*, p) sep \
	macro(uint32_t, size)

#define BVMOp_Memset(macro, sep) \
	macro(void*, pDst) sep \
	macro(uint8_t, val) sep \
	macro(uint32_t, size)

#define BVMOp_StackAlloc(macro, sep) \
	macro(uint32_t, size)

#define BVMOp_StackFree(macro, sep) \
	macro(uint32_t, size)

#define BVMOp_Heap_Alloc(macro, sep) \
	macro(uint32_t, size)

#define BVMOp_Heap_Free(macro, sep) \
	macro(void*, pPtr)

#define BVMOp_LoadVar(macro, sep) \
	macro(const void*, pKey) sep \
	macro(uint32_t, nKey) sep \
	macro(void*, pVal) sep \
	macro(uint32_t, nVal)

#define BVMOp_SaveVar(macro, sep) \
	macro(const void*, pKey) sep \
	macro(uint32_t, nKey) sep \
	macro(const void*, pVal) sep \
	macro(uint32_t, nVal)

#define BVMOp_CallFar(macro, sep) \
	macro(const ContractID&, cid) sep \
	macro(uint32_t, iMethod) sep \
	macro(void*, pArgs)

#define BVMOp_Halt(macro, sep)

#define BVMOp_AddSig(macro, sep) \
	macro(const PubKey&, pubKey)

#define BVMOp_FundsLock(macro, sep) \
	macro(AssetID, aid) sep \
	macro(Amount, amount)

#define BVMOp_FundsUnlock(macro, sep) \
	macro(AssetID, aid) sep \
	macro(Amount, amount)

#define BVMOp_RefAdd(macro, sep) \
	macro(const ContractID&, cid)

#define BVMOp_RefRelease(macro, sep) \
	macro(const ContractID&, cid)

#define BVMOp_AssetCreate(macro, sep) \
	macro(const void*, pMeta) sep \
	macro(uint32_t, nMeta)

#define BVMOp_AssetEmit(macro, sep) \
	macro(AssetID, aid) sep \
	macro(Amount, amount) sep \
	macro(uint8_t, bEmit)

#define BVMOp_AssetDestroy(macro, sep) \
	macro(AssetID, aid)

#define BVMOp_get_Height(macro, sep)

#define BVMOp_LoadVarEx(macro, sep) \
	macro(uint8_t, nTag) sep \
	macro(const void*, pKey) sep \
	macro(uint32_t, nKey) sep \
	macro(void*, pVal) sep \
	macro(uint32_t, nVal)

//#define BVMOp_LoadAllVars(macro, sep) \
//	macro(ILoadVarCallback*, pCallback)

#define BVMOp_VarsEnum(macro, sep) \
	macro(uint8_t, nTag0) sep \
	macro(const void*, pKey0) sep \
	macro(uint32_t, nKey0) sep \
	macro(uint8_t, nTag1) sep \
	macro(const void*, pKey1) sep \
	macro(uint32_t, nKey1)

#define BVMOp_VarsMoveNext(macro, sep) \
	macro(uint8_t*, pnTag) sep \
	macro(const void**, ppKey) sep \
	macro(uint32_t*, pnKey) sep \
	macro(const void**, ppVal) sep \
	macro(uint32_t*, pnVal)

#define BVMOpsAll_Common(macro) \
	macro(0x10, void*    , Memcpy) \
	macro(0x11, void*    , Memset) \
	macro(0x12, int32_t  , Memcmp) \
	macro(0x13, uint8_t  , Memis0) \
	macro(0x18, void*    , StackAlloc) \
	macro(0x19, void     , StackFree) \
	macro(0x1A, void*    , Heap_Alloc) \
	macro(0x1B, void     , Heap_Free) \
	macro(0x28, void     , Halt) \
	macro(0x40, Height   , get_Height) \

#define BVMOpsAll_Contract(macro) \
	macro(0x20, uint32_t , LoadVar) \
	macro(0x21, void     , SaveVar) \
	macro(0x23, void     , CallFar) \
	macro(0x29, void     , AddSig) \
	macro(0x30, void     , FundsLock) \
	macro(0x31, void     , FundsUnlock) \
	macro(0x32, uint8_t  , RefAdd) \
	macro(0x33, uint8_t  , RefRelease) \
	macro(0x38, AssetID  , AssetCreate) \
	macro(0x39, uint8_t  , AssetEmit) \
	macro(0x3A, uint8_t  , AssetDestroy) \

#define BVMOpsAll_Manager(macro) \
	macro(0x50, uint32_t , LoadVarEx) \
	macro(0x51, void     , VarsEnum) \
	macro(0x52, uint8_t  , VarsMoveNext)
