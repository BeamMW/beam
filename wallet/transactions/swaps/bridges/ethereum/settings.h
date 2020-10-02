// Copyright 2020 The Beam Team
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
#include <vector>

namespace beam::ethereum
{
struct Settings
{
    std::string m_address = "";
    std::vector<std::string> m_secretWords = {};
    uint32_t m_accountIndex = 0;
    bool m_shouldConnect = false;
    uint16_t m_txMinConfirmations = 12;
    uint32_t m_lockTimeInBlocks = 12 * 60 * 4;  // 12h
    double m_blocksPerHour = 250;

    bool IsInitialized() const;
    bool IsActivated() const;

    uint16_t GetTxMinConfirmations() const;
    uint32_t GetLockTimeInBlocks() const;
    double GetBlocksPerHour() const;
};
} // namespace beam::ethereum