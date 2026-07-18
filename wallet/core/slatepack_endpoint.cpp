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

    void SlatepackEndpoint::Send(const WalletID& peerID, const SetTxParameter& msg)
    {
        // A manual tx torn down (cancel/expire) notifies the peer with a FailureReason — nothing
        // useful to hand-deliver, so don't armor it. Also drop the stored outgoing Slatepack: the
        // tx is terminal, so its armored negotiation message is dead data.
        for (const auto& p : msg.m_Parameters)
            if (p.first == TxParameterID::FailureReason)
            {
                m_WalletDB->delTxParameter(m_CurrentTxID, kDefaultSubTxID, TxParameterID::SlatepackOutgoing);
                return;
            }

        m_LiveSend = true;
        BaseMessageEndpoint::Send(peerID, msg);
        m_LiveSend = false;
    }

    void SlatepackEndpoint::SendRawMessage(const WalletID& peerID, ByteBuffer&& encrypted)
    {
        // Armor only a live send routed through Send() above. ProcessStoredMessages replays every
        // stored SBBS message to every endpoint on startup; those aren't ours to armor.
        if (!m_LiveSend)
            return;

        slatepack::TxNegotiation n;
        n.m_Peer = peerID;
        n.m_Ciphertext = std::move(encrypted);

        const std::string armored = slatepack::Armor(slatepack::PayloadType::TxNegotiation, slatepack::ToBytes(n));

        // Persist the latest outgoing Slatepack so the user can re-copy it after dismissing the
        // produce dialog; survives a wallet restart (manual transfers are long-lived). Notify so
        // the tx list reloads with the param now, not only on the next tx change (the peer reply).
        storage::setTxParameter(*m_WalletDB, m_CurrentTxID, TxParameterID::SlatepackOutgoing, armored, true);

        if (m_OnOutgoing)
            m_OnOutgoing(m_CurrentTxID, armored);
    }

    bool SlatepackEndpoint::Preview(const std::string& armoredText, std::string& error, ImportInfo& info)
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

        // Decrypt with a subscribed own-address key but do NOT hand it to the wallet yet — the
        // user confirms first. A Slatepack none of our addresses can decrypt is for another wallet.
        SetTxParameter decrypted;
        WalletID myAddr = Zero;
        if (!ProcessMessage(msg, &decrypted, &myAddr, false))
        {
            error = "This Slatepack isn't addressed to your wallet.";
            return false;
        }

        info.m_TxID = std::to_string(decrypted.m_TxID);

        if (auto tx = m_WalletDB->getTx(decrypted.m_TxID))
        {
            // Our side of the tx already exists (e.g. we sent S1 and are importing the reply).
            info.m_Amount      = tx->m_amount;
            info.m_AssetID     = tx->m_assetId;
            info.m_Fee         = tx->m_fee;
            info.m_IsSend      = tx->m_sender;
            info.m_AddressFrom = tx->getAddressFrom();
            info.m_AddressTo   = tx->getAddressTo();
        }
        else
        {
            // First look at an incoming invitation — summarise straight from the message.
            decrypted.GetParameter(TxParameterID::Amount, info.m_Amount);
            decrypted.GetParameter(TxParameterID::AssetID, info.m_AssetID);
            decrypted.GetParameter(TxParameterID::Fee, info.m_Fee);
            info.m_IsSend      = false;
            info.m_AddressFrom = std::to_string(decrypted.m_From);
            info.m_AddressTo   = std::to_string(myAddr);
        }

        // Hold the message until the user confirms (Commit) or discards (CancelPending).
        m_PendingImports[info.m_TxID] = std::move(msg);
        return true;
    }

    bool SlatepackEndpoint::Commit(const std::string& txId, std::string& error)
    {
        auto it = m_PendingImports.find(txId);
        if (it == m_PendingImports.end())
        {
            error = "no pending Slatepack to confirm";
            return false;
        }
        ProcessMessage(it->second); // deliver to the wallet — the transaction proceeds
        m_PendingImports.erase(it);
        return true;
    }

    void SlatepackEndpoint::CancelPending(const std::string& txId)
    {
        m_PendingImports.erase(txId);
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
