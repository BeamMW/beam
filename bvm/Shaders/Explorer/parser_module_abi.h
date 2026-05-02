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

#include "../common.h"
#include "parser_common.h"

// Per-contract explorer parser module ABI.
//
// Every parser module exports four BEAM_EXPORT entrypoints:
//
//   Method_0(sid, cid, iMethod, pArgs, nArgs)  — parse a method invocation.
//                                                Mirrors monolith Method_0.
//   Method_1(sid, cid)                         — emit kind / identification only.
//                                                Mirrors monolith Method_1.
//   Method_2(sid, cid)                         — emit full state.
//                                                Mirrors monolith Method_2.
//   Method_3(out_buf, out_cap)                 — return the SIDs this module handles.
//                                                Two-call protocol:
//                                                  call with out_buf=nullptr, out_cap=0
//                                                  -> module returns N (count of SIDs).
//                                                  Host allocates N * sizeof(ShaderID),
//                                                  calls again -> module writes
//                                                  min(N, out_cap) entries and returns
//                                                  the actual count written.
//
// Method_0/1/2 keep monolith call shape so the host's existing wasm-call paths
// (ProcessorInfoParser::ParseExtraInfo / get_ContractDescr) keep working
// unchanged for per-module dispatch. Method_3 is new.
//
// A module may declare multiple SIDs (versioned contracts ship every entry of
// their s_pSID[] in one module). The module recovers iVer by scanning its own
// s_pSID[] inside Method_0/Method_2.
//
// All output goes to the host's doc stream via Env::Doc* and the helpers in
// parser_common.h. State lookups go through Env::VarReader keyed by the
// passed-in cid. Modules are restricted to the manager-mode Env import surface.

// Convenience helper: copy `n` SIDs from `src` to `out_buf`, capped by `out_cap`.
// Returns `n` regardless of cap (the two-call protocol relies on the unclamped count).
inline uint32_t ParserModule_FillSids(ShaderID* out_buf, uint32_t out_cap, const ShaderID* src, uint32_t n)
{
	if (out_buf)
	{
		uint32_t nCopy = (n < out_cap) ? n : out_cap;
		for (uint32_t i = 0; i < nCopy; i++)
			_POD_(out_buf[i]) = src[i];
	}
	return n;
}

// Boilerplate macros for the common module shape.
// Use only when Method_1 and Method_2 are exactly `OnKind()` (no per-cid state body).

#define PARSER_MODULE_EXPORT_SIDS(SIDS) \
	BEAM_EXPORT uint32_t Method_3(ShaderID* out_buf, uint32_t out_cap) { \
		return ParserModule_FillSids(out_buf, out_cap, SIDS, _countof(SIDS)); }

#define PARSER_MODULE_EXPORT_KIND_ONLY(FN_KIND) \
	BEAM_EXPORT void Method_1(const ShaderID&, const ContractID&) { Env::DocGroup gr(""); FN_KIND(); } \
	BEAM_EXPORT void Method_2(const ShaderID&, const ContractID&) { Env::DocGroup gr(""); FN_KIND(); }
