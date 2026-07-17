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

#include "wallet/core/slatepack_endpoint.h"
#include "wallet/core/slatepack.h"
#include "utility/logger.h"

namespace beam::wallet
{
    SlatepackEndpoint::SlatepackEndpoint(IWalletMessageConsumer& wallet, const IWalletDB::Ptr& walletDB, OutgoingHandler handler)
        : BaseMessageEndpoint(wallet, walletDB)
        , m_WalletDB(walletDB)
        , m_OnOutgoing(std::move(handler))
    {
        Subscribe();
        m_WalletDB->Subscribe(this);

        // Drain Slatepacks queued while the wallet couldn't decrypt them (read-only path). A hot
        // wallet decrypts inline, so this is normally empty.
        for (const auto& m : m_WalletDB->getIncomingWalletMessages())
        {
            proto::BbsMsg msg;
            msg.m_Channel = m.m_Channel;
            msg.m_TimePosted = getTimestamp();
            msg.m_Message = m.m_Message;
            ProcessMessage(msg);
            m_WalletDB->deleteIncomingWalletMessage(m.m_ID);
        }
    }

    SlatepackEndpoint::~SlatepackEndpoint()
    {
        try
        {
            m_WalletDB->Unsubscribe(this);
            Unsubscribe();
        }
        catch (const std::exception& e)
        {
            BEAM_LOG_UNHANDLED_EXCEPTION() << "what = " << e.what();
        }
        catch (...)
        {
            BEAM_LOG_UNHANDLED_EXCEPTION();
        }
    }

    bool SlatepackEndpoint::AcceptsMessage(const TxID& txID)
    {
        // Inverse of the base endpoint: handle ONLY transactions flagged for manual transport.
        bool isManual = false;
        storage::getTxParameter(*m_WalletDB, txID, TxParameterID::ManualTransport, isManual);
        if (isManual)
            // Stash the txID for the SendRawMessage that follows synchronously in this Send()
            // call (it sees only the encrypted body, but must label the produced Slatepack).
            m_CurrentTxID = txID;
        return isManual;
    }

    void SlatepackEndpoint::SendRawMessage(const WalletID& peerID, ByteBuffer&& encrypted)
    {
        slatepack::TxNegotiation n;
        n.m_Peer = peerID;
        n.m_Ciphertext = std::move(encrypted);

        // Persist in the outgoing queue so an unsent Slatepack survives a restart.
        m_WalletDB->saveWalletMessage(peerID, Blob(n.m_Ciphertext));

        if (m_OnOutgoing)
            m_OnOutgoing(m_CurrentTxID, slatepack::Armor(slatepack::PayloadType::TxNegotiation, slatepack::ToBytes(n)));
    }

    bool SlatepackEndpoint::Inject(const std::string& armoredText, std::string& error)
    {
        slatepack::PayloadType type;
        ByteBuffer payload;
        if (!slatepack::Unarmor(armoredText, type, payload, error))
            return false;

        if (type != slatepack::PayloadType::TxNegotiation)
        {
            error = "unsupported Slatepack type";
            return false;
        }

        slatepack::TxNegotiation n;
        if (!slatepack::FromBytes(n, payload))
        {
            error = "damaged Slatepack (payload)";
            return false;
        }

        proto::BbsMsg msg;
        n.m_Peer.m_Channel.Export(msg.m_Channel);
        msg.m_TimePosted = getTimestamp();
        msg.m_Message = std::move(n.m_Ciphertext);

        // Matches a subscribed own-address channel, decrypts with that address's key, and
        // feeds the negotiator. A Slatepack for another wallet matches no channel -> no-op.
        ProcessMessage(msg);
        return true;
    }

    void SlatepackEndpoint::onAddressChanged(ChangeAction action, const std::vector<WalletAddress>& items)
    {
        // Keep the set of listening channels current as the user creates/expires addresses,
        // mirroring WalletNetworkViaBbs so a receive address created after startup still works.
        switch (action)
        {
        case ChangeAction::Added:
        case ChangeAction::Updated:
            for (const auto& address : items)
            {
                if (!address.isOwn())
                    continue;
                if (!address.isExpired())
                    AddOwnAddress(address);
                else
                    DeleteOwnAddress(address.m_BbsAddr);
            }
            break;
        case ChangeAction::Removed:
            for (const auto& address : items)
                if (address.isOwn())
                    DeleteOwnAddress(address.m_BbsAddr);
            break;
        default:
            break;
        }
    }
}
