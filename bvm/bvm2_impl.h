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
#include "bvm2.h"
#include "bvm2_opcodes.h"

namespace beam {
namespace bvm2 {

	struct ProcessorPlusEnv
		:public Processor
	{
		typedef ECC::Point PubKey;
		typedef Asset::ID AssetID;

#define MACRO_COMMA ,
#define PAR_DECL(type, name) type name
#define THE_MACRO(id, ret, name) \
		ret OnHost_##name(BVMOp_##name(PAR_DECL, MACRO_COMMA));
		BVMOpsAll(THE_MACRO)
#undef THE_MACRO
#undef PAR_DECL
#undef MACRO_COMMA
	};


} // namespace bvm2
} // namespace beam
