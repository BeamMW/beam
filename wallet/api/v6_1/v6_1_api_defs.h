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

namespace beam::wallet
{
    #define V6_1_API_METHODS(macro) \
        macro(EvSubUnsub,        "ev_subunsub",     API_READ_ACCESS,  API_SYNC,  APPS_ALLOWED) \
        macro(GetVersion,        "get_version",     API_READ_ACCESS,  API_SYNC,  APPS_ALLOWED) \
        macro(WalletStatusV61,   "wallet_status",   API_READ_ACCESS,  API_SYNC,  APPS_ALLOWED) \
        macro(InvokeContractV61, "invoke_contract", API_WRITE_ACCESS, API_ASYNC, APPS_ALLOWED)

    struct EvSubUnsub
    {
        boost::optional<bool> syncProgress   = boost::none;
        boost::optional<bool> systemState    = boost::none;
        boost::optional<bool> assetChanged   = boost::none;
        boost::optional<bool> utxosChanged   = boost::none;
        boost::optional<bool> addrsChanged   = boost::none;
        boost::optional<bool> txsChanged     = boost::none;
        boost::optional<bool> connectChanged = boost::none;

        struct Response
        {
            bool result;
        };
    };

    struct GetVersion
    {
        struct Response
        {
            std::string apiVersion;
            unsigned long apiVersionMinor;
            unsigned long apiVersionMajor;

            std::string beamVersion;
            unsigned long beamVersionMajor;
            unsigned long beamVersionMinor;
            unsigned long beamVersionRevision;

            std::string beamCommitHash;
            std::string beamBranchName;
            std::string beamNetworkName;
        };
    };

    struct WalletStatusV61
    {
        boost::optional<bool> nzOnly = boost::none;
        struct Response
        {
            beam::Height currentHeight = 0;
            Merkle::Hash currentStateHash = Zero;
            Timestamp currentStateTimestamp = 0;
            Merkle::Hash prevStateHash = Zero;
            double difficulty = 0;
            bool isInSync = false;
            Amount available = 0;
            Amount receiving = 0;
            Amount sending = 0;
            Amount maturing = 0;
            boost::optional<storage::Totals> totals = boost::none;
        };
    };

    struct InvokeContractV61
    {
        std::vector<uint8_t> contract;
        std::string args;
        bool createTx = true;
        uint32_t priority = 0;
        uint32_t unique = 0;

        struct Response
        {
            boost::optional<std::string> output;
            boost::optional<beam::ByteBuffer> invokeData;
            boost::optional<TxID> txid = TxID();
        };
    };
}
