// Copyright 2021 The Beam Team
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

#include <string>
#include "wallet/core/wallet_db.h"

namespace beam::wallet
{
std::string ExportTxHistoryToCsv(const IWalletDB& db);
#ifdef BEAM_ATOMIC_SWAP_SUPPORT
std::string ExportAtomicSwapTxHistoryToCsv(const IWalletDB& db);
#endif // BEAM_ATOMIC_SWAP_SUPPORT
#ifdef BEAM_ASSET_SWAP_SUPPORT
std::string ExportAssetsSwapTxHistoryToCsv(const IWalletDB& db);
#endif  // BEAM_ASSET_SWAP_SUPPORT
std::string ExportContractTxHistoryToCsv(const IWalletDB& db);
} // namespace beam::wallet
