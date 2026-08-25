// Copyright 2018-2026 The Beam Team
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

#include "wallet/core/wallet_network.h" // BaseMessageEndpoint
#include "wallet/core/wallet_db.h"      // IWalletDbObserver, IWalletDB, WalletAddress
#include "wallet/core/slatepack.h"      // slatepack::Error
#include <functional>
#include <string>
#include <map>

namespace beam::wallet
{
    // Manual, node-independent transaction transport: queues each negotiation message and
    // surfaces it as an armored Slatepack instead of posting to SBBS; pasted Slatepacks re-enter
    // through the same path an SBBS message would. Handles only ManualTransport-flagged txs; the
    // SBBS endpoint skips those (BaseMessageEndpoint::AcceptsMessage), so both can run at once.
    class SlatepackEndpoint
        : public BaseMessageEndpoint
        , private IWalletDbObserver
    {
    public:
        // Called on the wallet reactor thread when a new outgoing Slatepack is produced.
        using OutgoingHandler = std::function<void(const TxID&, const std::string& armored)>;

        SlatepackEndpoint(IWalletMessageConsumer&, const IWalletDB::Ptr&, OutgoingHandler);
        ~SlatepackEndpoint() override;

        // Structured summary of what an imported Slatepack contained, for the UI to render.
        struct ImportInfo
        {
            Amount      m_Amount  = 0;
            Asset::ID   m_AssetID = 0;
            Amount      m_Fee     = 0;
            bool        m_IsSend  = false; // our role in the imported tx
            std::string m_AddressFrom;     // sender address
            std::string m_AddressTo;       // receiver address
            std::string m_TxID;            // hex transaction id
        };

        // Decrypt a pasted Slatepack and preview it WITHOUT continuing the transaction: fills
        // 'info', stashes the message pending confirmation (keyed by info.m_TxID), returns true.
        // A Slatepack none of our addresses can decrypt is a failure here, not a silent no-op.
        bool Preview(const std::string& armoredText, slatepack::Error& error, ImportInfo& info);

        // Confirm a previewed Slatepack: hand the stashed message to the wallet so the
        // transaction proceeds. Returns false if no pending import matches txId.
        bool Commit(const std::string& txId, slatepack::Error& error);

        // Discard a previewed-but-unconfirmed Slatepack.
        void CancelPending(const std::string& txId);

    private:
        // BaseMessageEndpoint
        bool AcceptsMessage(const TxID& txID) override;
        void Send(const WalletID& peerID, const SetTxParameter& msg) override;
        void SendRawMessage(const WalletID& peerID, ByteBuffer&&) override;
        // IWalletDbObserver
        void onAddressChanged(ChangeAction action, const std::vector<WalletAddress>& items) override;

        void ExpirePendingImports();

        IWalletDB::Ptr m_WalletDB;
        OutgoingHandler m_OnOutgoing;

        // Slatepacks decrypted for preview, awaiting the user's confirm/cancel, keyed by txID.
        // Bounded and time-limited: a preview the user simply walks away from must not pin the
        // ciphertext for the lifetime of the process.
        struct Pending
        {
            proto::BbsMsg m_Msg;
            Timestamp     m_Created = 0;
        };
        std::map<std::string, Pending> m_PendingImports;

        static const size_t    s_MaxPendingImports  = 16;
        static const Timestamp s_PendingImportTtl_s = 60 * 60; // an hour is plenty to click Send
    };
}
