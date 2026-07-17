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
#include <functional>
#include <string>

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

        // Import a pasted/scanned/opened Slatepack. On success feeds the contained negotiation
        // message to the wallet and returns true. On any failure returns false and sets
        // 'error' to a short user-facing reason. A Slatepack addressed to another wallet
        // (no matching channel) is not an error here — it is a silent no-op.
        bool Inject(const std::string& armoredText, std::string& error);

    private:
        // BaseMessageEndpoint
        bool AcceptsMessage(const TxID& txID) override;
        void SendRawMessage(const WalletID& peerID, ByteBuffer&&) override;
        // IWalletDbObserver
        void onAddressChanged(ChangeAction action, const std::vector<WalletAddress>& items) override;

        IWalletDB::Ptr m_WalletDB;
        OutgoingHandler m_OnOutgoing;
        TxID m_CurrentTxID = {};
    };
}
