// Copyright 2019 The Beam Team
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

#include "negotiator.h"

namespace beam::Lightning
{

struct Codes
	:public Negotiator::Codes
{
	static const uint32_t Control0 = 1024 << 16;

	static const uint32_t Revision = Control0 + 3;

	static const uint32_t Fee = Control0 + 11; // all txs
	static const uint32_t H0 = Control0 + 12;
	static const uint32_t H1 = Control0 + 13;
	static const uint32_t HLock = Control0 + 14;
	static const uint32_t HLifeTime = Control0 + 15;
	static const uint32_t HPostLockReserve = Control0 + 16;

	static const uint32_t ValueMy = Control0 + 21;
	static const uint32_t ValueYours = Control0 + 22;
	static const uint32_t ValueTansfer = Control0 + 25;
	static const uint32_t CloseGraceful = Control0 + 31;
};
}  // namespace beam::Lightning
