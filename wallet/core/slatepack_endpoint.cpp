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
#include "utility/logger.h"

namespace beam::wallet
{
    namespace
    {
        // Parse the hex tx id the UI/CLI round-trips through ImportInfo::m_TxID.
        bool TxIDFromHex(const std::string& s, TxID& out)
        {
            const auto v = from_hex(s);
            if (v.size() != out.size())
                return false;
            std::copy(v.begin(), v.end(), out.begin());
            return true;
        }
    }

    SlatepackEndpoint::SlatepackEndpoint(IWalletMessageConsumer& wallet, const IWalletDB::Ptr& walletDB, OutgoingHandler handler)
        : BaseMessageEndpoint(wallet, walletDB)
        , m_WalletDB(walletDB)
        , m_OnOutgoing(std::move(handler))
    {
        Subscribe();
        m_WalletDB->Subscribe(this);
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
        return isManual;
    }

    void SlatepackEndpoint::Send(const WalletID& peerID, const SetTxParameter& msg)
    {
        if (!AcceptsMessage(msg.m_TxID))
            return; // not a manual tx - the SBBS endpoint carries it

        // A manual tx torn down (cancel/expire) notifies the peer with a FailureReason - nothing
        // useful to hand-deliver, so don't armor it. Also drop the stored outgoing Slatepack for
        // THAT tx: it is terminal, so its armored negotiation message is dead data.
        for (const auto& p : msg.m_Parameters)
            if (p.first == TxParameterID::FailureReason)
            {
                m_WalletDB->delTxParameter(msg.m_TxID, kDefaultSubTxID, TxParameterID::SlatepackOutgoing);
                return;
            }

        BaseMessageEndpoint::Send(peerID, msg);
    }

    void SlatepackEndpoint::SendRawMessage(const WalletID& peerID, ByteBuffer&& encrypted)
    {
        // Armor only a live negotiation message routed through Send() above, which is the only
        // caller that knows which tx this ciphertext belongs to. Wallet::ProcessStoredMessages
        // replays raw stored buffers into every endpoint on startup; those aren't ours to armor.
        const TxID* pTxID = GetSendingTxID();
        if (!pTxID)
            return;
        const TxID txID = *pTxID;

        slatepack::TxNegotiation n;
        n.m_Peer = peerID;
        n.m_Ciphertext = std::move(encrypted);

        const std::string armored = slatepack::Armor(slatepack::PayloadType::TxNegotiation, slatepack::ToBytes(n));

        // Persist the latest outgoing Slatepack so the user can re-copy it after dismissing the
        // produce dialog; survives a wallet restart (manual transfers are long-lived). Notify so
        // the tx list reloads with the param now, not only on the next tx change (the peer reply).
        storage::setTxParameter(*m_WalletDB, txID, TxParameterID::SlatepackOutgoing, armored, true);

        if (m_OnOutgoing)
            m_OnOutgoing(txID, armored);
    }

    void SlatepackEndpoint::ExpirePendingImports()
    {
        const Timestamp now = getTimestamp();
        for (auto it = m_PendingImports.begin(); it != m_PendingImports.end(); )
        {
            if (now > it->second.m_Created + s_PendingImportTtl_s)
                it = m_PendingImports.erase(it);
            else
                ++it;
        }

        // Hard cap as well: a stream of previews within the TTL must not grow without bound.
        while (m_PendingImports.size() >= s_MaxPendingImports)
        {
            auto oldest = m_PendingImports.begin();
            for (auto it = m_PendingImports.begin(); it != m_PendingImports.end(); ++it)
                if (it->second.m_Created < oldest->second.m_Created)
                    oldest = it;
            m_PendingImports.erase(oldest);
        }
    }

    bool SlatepackEndpoint::Preview(const std::string& armoredText, slatepack::Error& error, ImportInfo& info)
    {
        slatepack::PayloadType type;
        ByteBuffer payload;
        if (!slatepack::Unarmor(armoredText, type, payload, error))
            return false;

        if (type != slatepack::PayloadType::TxNegotiation)
        {
            error = slatepack::Error::UnsupportedType;
            return false;
        }

        slatepack::TxNegotiation n;
        if (!slatepack::FromBytes(n, payload))
        {
            error = slatepack::Error::BadPayload;
            return false;
        }

        proto::BbsMsg msg;
        n.m_Peer.m_Channel.Export(msg.m_Channel);
        msg.m_TimePosted = getTimestamp();
        msg.m_Message = std::move(n.m_Ciphertext);

        // Decrypt with a subscribed own-address key but do NOT hand it to the wallet yet - the
        // user confirms first. A Slatepack none of our addresses can decrypt is for another wallet.
        SetTxParameter decrypted;
        WalletID myAddr = Zero;
        switch (ProcessMessage(msg, &decrypted, &myAddr, false))
        {
        case MsgResult::Decrypted:
            break;
        case MsgResult::ReadOnly:
            error = slatepack::Error::ReadOnlyWallet;
            return false;
        case MsgResult::Handler:
            error = slatepack::Error::HandlerAddress;
            return false;
        default:
            error = slatepack::Error::NotForThisWallet;
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
            // First look at an incoming invitation - summarise straight from the message.
            decrypted.GetParameter(TxParameterID::Amount, info.m_Amount);
            decrypted.GetParameter(TxParameterID::AssetID, info.m_AssetID);
            decrypted.GetParameter(TxParameterID::Fee, info.m_Fee);
            info.m_IsSend      = false;
            info.m_AddressFrom = std::to_string(decrypted.m_From);
            info.m_AddressTo   = std::to_string(myAddr);
        }

        // Hold the message until the user confirms (Commit) or discards (CancelPending).
        ExpirePendingImports();
        Pending& p = m_PendingImports[info.m_TxID];
        p.m_Msg     = std::move(msg);
        p.m_Created = getTimestamp();

        error = slatepack::Error::None;
        return true;
    }

    bool SlatepackEndpoint::Commit(const std::string& txId, slatepack::Error& error)
    {
        auto it = m_PendingImports.find(txId);
        if (it == m_PendingImports.end())
        {
            error = slatepack::Error::NoPendingImport;
            return false;
        }

        const proto::BbsMsg msg = std::move(it->second.m_Msg);
        m_PendingImports.erase(it);

        // Flag the tx for manual transport BEFORE delivering it. The negotiator produces its
        // reply synchronously from inside OnWalletMessage, and both endpoints consult this
        // parameter to decide who carries that reply - so it has to be committed first, or the
        // SBBS endpoint would post the reply as well.
        //
        // Setting it here, rather than trusting a ManualTransport parameter carried in the
        // message, is deliberate: the flag means "this arrived by hand", which only the endpoint
        // that received it can know. A peer must not be able to push us off SBBS.
        TxID txID;
        if (TxIDFromHex(txId, txID))
            storage::setTxParameter(*m_WalletDB, txID, TxParameterID::ManualTransport, true, false);

        ProcessMessage(msg); // deliver to the wallet - the transaction proceeds
        error = slatepack::Error::None;
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
