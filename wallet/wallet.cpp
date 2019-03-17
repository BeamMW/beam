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

#include "wallet.h"
#include <boost/uuid/uuid.hpp>

#include "core/ecc_native.h"
#include "core/block_crypt.h"
#include "utility/logger.h"
#include "utility/helpers.h"
#include "swap_transaction.h"
#include <algorithm>
#include <random>
#include <iomanip>
#include <numeric>

namespace std
{
    string to_string(const beam::WalletID& id)
    {
        static_assert(sizeof(id) == sizeof(id.m_Channel) + sizeof(id.m_Pk), "");

		char szBuf[sizeof(id) * 2 + 1];
		beam::to_hex(szBuf, &id, sizeof(id));

		const char* szPtr = szBuf;
		while (*szPtr == '0')
			szPtr++;

		if (!*szPtr)
			szPtr--; // leave at least 1 symbol

		return szPtr;
	}
}

namespace beam
{
    using namespace wallet;
    using namespace std;
    using namespace ECC;

    int WalletID::cmp(const WalletID& x) const
    {
        int n = m_Channel.cmp(x.m_Channel);
        if (n)
            return n;
        return m_Pk.cmp(x.m_Pk);
    }

    bool WalletID::FromBuf(const ByteBuffer& x)
    {
        if (x.size() > sizeof(*this))
            return false;

        typedef uintBig_t<sizeof(*this)> BigSelf;
        static_assert(sizeof(BigSelf) == sizeof(*this), "");

        *reinterpret_cast<BigSelf*>(this) = Blob(x);
        return true;
    }

    bool WalletID::FromHex(const std::string& s)
    {
        bool bValid = true;
        ByteBuffer bb = from_hex(s, &bValid);

        return bValid && FromBuf(bb);
    }

    bool WalletID::IsValid() const
    {
        Point::Native p;
        return proto::ImportPeerID(p, m_Pk);
    }

    bool check_receiver_address(const std::string& addr)
    {
        WalletID walletID;
        return
            walletID.FromHex(addr) &&
            walletID.IsValid();
    }

    std::ostream& operator<<(std::ostream& os, const TxID& uuid)
    {
        os << "[" << to_hex(uuid.data(), uuid.size()) << "]";
        return os;
    }

    std::ostream& operator<<(std::ostream& os, const PrintableAmount& amount)
    {
        const string_view beams{ " beams " };
        const string_view chattles{ " groth " };
        auto width = os.width();

        if (amount.m_showPoint)
        {
            const char origFill = os.fill();
            Amount groths = amount.m_value % Rules::Coin;
            std::string grothsString { '0' };
            size_t grothsLength = 1;

            if (groths > 0)
            {
                std::string grothsText = std::to_string(groths);
                size_t maxGrothsLength = std::lround(std::log10(Rules::Coin));

                // additional length for add leading zeros
                assert(maxGrothsLength >= grothsText.length());
                size_t additionalLength = maxGrothsLength - grothsText.length();

                // trim trailing zeros
                grothsText.erase(grothsText.find_last_not_of('0') + 1, std::string::npos);

                grothsLength = additionalLength + grothsText.length();
                grothsString = grothsText;
            }

            assert(width > static_cast<decltype(width)>(grothsLength + beams.length()));

            os << setw(width - grothsLength - beams.length()) << Amount(amount.m_value / Rules::Coin)
                << "." << std::setfill('0') << setw(grothsLength) << grothsString << std::setfill(origFill)
                << beams.data();

            return os;
        }

        if (amount.m_value >= Rules::Coin)
        {
            os << setw(width - beams.length()) << Amount(amount.m_value / Rules::Coin) << beams.data();
        }
        Amount c = amount.m_value % Rules::Coin;
        if (c > 0 || amount.m_value == 0)
        {
            os << setw(width - chattles.length()) << c << chattles.data();
        }
        return os;
    }

	void PaymentConfirmation::get_Hash(Hash::Value& hv) const
	{
		Hash::Processor()
			<< "PaymentConfirmation"
			<< m_KernelID
			<< m_Sender
			<< m_Value
			>> hv;
	}

	bool PaymentConfirmation::IsValid(const PeerID& pid) const
	{
		Point::Native pk;
		if (!proto::ImportPeerID(pk, pid))
			return false;

		Hash::Value hv;
		get_Hash(hv);

		return m_Signature.IsValid(hv, pk);
	}

	void PaymentConfirmation::Sign(const Scalar::Native& sk)
	{
		Hash::Value hv;
		get_Hash(hv);

		m_Signature.Sign(hv, sk);
	}


    const char Wallet::s_szNextUtxoEvt[] = "NextUtxoEvent";

    Wallet::Wallet(IWalletDB::Ptr walletDB, TxCompletedAction&& action)
        : m_WalletDB{ walletDB }
        , m_pNodeNetwork(nullptr)
        , m_pWalletNetwork(nullptr)
        , m_TxCompletedAction{move(action)}
        , m_LastSyncTotal(0)
        , m_OwnedNodesOnline(0)
    {
        assert(walletDB);
        ResumeAllTransactions();
    }

    void Wallet::get_Kdf(Key::IKdf::Ptr& pKdf)
    {
        pKdf = m_WalletDB->get_MasterKdf();
    }

    void Wallet::OnOwnedNode(const PeerID& id, bool bUp)
    {
        if (bUp)
        {
            if (!m_OwnedNodesOnline++)
                RequestUtxoEvents(); // maybe time to refresh
        }
        else
        {
            assert(m_OwnedNodesOnline);
            if (!--m_OwnedNodesOnline)
                AbortUtxoEvents();
        }
    }

    Block::SystemState::IHistory& Wallet::get_History()
    {
        return m_WalletDB->get_History();
    }

    void Wallet::set_Network(proto::FlyClient::INetwork& netNode, IWalletNetwork& netWallet)
    {
        m_pNodeNetwork = &netNode;
        m_pWalletNetwork = &netWallet;
    }

    void Wallet::initBitcoin(io::Reactor& reactor, const std::string& userName, const std::string& pass, const io::Address& address)
    {
        m_bitcoinRPC = make_shared<BitcoinRPC>(reactor, userName, pass, address);
    }

    Wallet::~Wallet()
    {
        // clear all requests
#define THE_MACRO(type, msgOut, msgIn) \
        while (!m_Pending##type.empty()) \
            DeleteReq(*m_Pending##type.begin());

        REQUEST_TYPES_All(THE_MACRO)
#undef THE_MACRO
    }

    TxID Wallet::transfer_money(const WalletID& from, const WalletID& to, Amount amount, Amount fee, bool sender, Height lifetime, ByteBuffer&& message)
    {
        return transfer_money(from, to, AmountList{ amount }, fee, {}, sender, lifetime, move(message));
    }

    TxID Wallet::transfer_money(const WalletID& from, const WalletID& to, Amount amount, Amount fee, const CoinIDList& coins, bool sender, Height lifetime, ByteBuffer&& message)
    {
        return transfer_money(from, to, AmountList{ amount }, fee, coins, sender, lifetime, move(message));
    }

    TxID Wallet::transfer_money(const WalletID& from, const WalletID& to, const AmountList& amountList, Amount fee, const CoinIDList& coins, bool sender, Height lifetime, ByteBuffer&& message)
    {
        auto receiverAddr = m_WalletDB->getAddress(to);

        if (receiverAddr)
        {
            if (receiverAddr->m_OwnID && receiverAddr->isExpired())
            {
                LOG_INFO() << "Can't send to the expired address.";
                throw AddressExpiredException();
            }
        }

        TxID txID = wallet::GenerateTxID();
        auto tx = constructTransaction(txID, TxType::Simple);
        Height currentHeight = m_WalletDB->getCurrentHeight();

        tx->SetParameter(TxParameterID::TransactionType, TxType::Simple, false);
        tx->SetParameter(TxParameterID::MaxHeight, currentHeight + lifetime, false); // transaction is valid +lifetime blocks from currentHeight
        tx->SetParameter(TxParameterID::IsInitiator, true, false);
        tx->SetParameter(TxParameterID::AmountList, amountList, false);
        tx->SetParameter(TxParameterID::PreselectedCoins, coins, false);

        TxDescription txDescription;

        txDescription.m_txId = txID;
        txDescription.m_amount = std::accumulate(amountList.begin(), amountList.end(), 0ULL);
        txDescription.m_fee = fee;
        txDescription.m_minHeight = currentHeight;
        txDescription.m_peerId = to;
        txDescription.m_myId = from;
        txDescription.m_message = move(message);
        txDescription.m_createTime = getTimestamp();
        txDescription.m_sender = sender;
        txDescription.m_status = TxStatus::Pending;
        txDescription.m_selfTx = (receiverAddr && receiverAddr->m_OwnID);
        m_WalletDB->saveTx(txDescription);

        m_Transactions.emplace(txID, tx);

        updateTransaction(txID);

        return txID;
    }

    TxID Wallet::split_coins(const WalletID& from, const AmountList& amountList, Amount fee, bool sender, Height lifetime,  ByteBuffer&& message)
    {
        return transfer_money(from, from, amountList, fee, {}, sender, lifetime, move(message));
    }

    TxID Wallet::swap_coins(const WalletID& from, const WalletID& to, Amount amount, Amount fee, wallet::AtomicSwapCoin swapCoin, Amount swapAmount)
    {
        auto txID = wallet::GenerateTxID();
        auto tx = constructTransaction(txID, TxType::AtomicSwap);

        tx->SetParameter(TxParameterID::TransactionType, TxType::AtomicSwap, false);
        tx->SetParameter(TxParameterID::CreateTime, getTimestamp(), false);
        tx->SetParameter(TxParameterID::Amount, amount, false);
        tx->SetParameter(TxParameterID::Fee, fee, false);
        tx->SetParameter(TxParameterID::MinHeight, m_WalletDB->getCurrentHeight(), false);
        tx->SetParameter(TxParameterID::PeerID, to, false);
        tx->SetParameter(TxParameterID::MyID, from, false);
        tx->SetParameter(TxParameterID::IsSender, true, false);
        tx->SetParameter(TxParameterID::IsInitiator, true, false);
        tx->SetParameter(TxParameterID::Status, TxStatus::Pending, true);

        tx->SetParameter(TxParameterID::AtomicSwapCoin, swapCoin, false);
        tx->SetParameter(TxParameterID::AtomicSwapAmount, swapAmount, false);

        m_Transactions.emplace(txID, tx);

        updateTransaction(txID);

        return txID;
    }

    void Wallet::Refresh()
    {
        m_WalletDB->clear();
        Block::SystemState::ID id;
        ZeroObject(id);
        m_WalletDB->setSystemStateID(id);

        SetUtxoEventsHeight(0);
        RequestUtxoEvents();
        RefreshTransactions();
    }

    void Wallet::RefreshTransactions()
    {
        auto txs = m_WalletDB->getTxHistory();
        for (auto& tx : txs)
        {
            if (m_Transactions.find(tx.m_txId) == m_Transactions.end())
            {
                auto t = constructTransaction(tx.m_txId, TxType::Simple);
                if (t->SetParameter(TxParameterID::KernelProofHeight, Height(0), false)
                    && t->SetParameter(TxParameterID::KernelUnconfirmedHeight, Height(0), false))
                {
                    m_Transactions.emplace(tx.m_txId, t);
                }
            }
        }
        auto t = m_Transactions;
        for (auto& p : t)
        {
            auto tx = p.second;
            tx->Update();
        }
    }

    void Wallet::ResumeTransaction(const TxDescription& tx)
    {
        if (tx.canResume() && m_Transactions.find(tx.m_txId) == m_Transactions.end())
        {
            auto t = constructTransaction(tx.m_txId, TxType::Simple);

            m_Transactions.emplace(tx.m_txId, t);
        }
    }

    void Wallet::ResumeAllTransactions()
    {
        auto txs = m_WalletDB->getTxHistory();
        for (auto& tx : txs)
        {
            ResumeTransaction(tx);
        }
    }

    void Wallet::on_tx_completed(const TxID& txID)
    {
		// Note: the passed TxID is (most probably) the member of the transaction, which we, most probably, are going to erase from the map, which can potentially delete it.
		// Make sure we either copy the txID, or prolong the lifetime of the tx.

		wallet::BaseTransaction::Ptr pGuard;

        auto it = m_Transactions.find(txID);
        if (it != m_Transactions.end())
        {
			pGuard.swap(it->second);
            m_Transactions.erase(it);
        }
 
        if (m_TxCompletedAction)
        {
            m_TxCompletedAction(txID);
        }
    }

    void Wallet::confirm_outputs(const vector<Coin>& coins)
    {
        for (auto& coin : coins)
            getUtxoProof(coin.m_ID);
    }

    bool Wallet::MyRequestUtxo::operator < (const MyRequestUtxo& x) const
    {
        return m_Msg.m_Utxo < x.m_Msg.m_Utxo;
    }

    bool Wallet::MyRequestKernel::operator < (const MyRequestKernel& x) const
    {
        return m_TxID < x.m_TxID;
    }

    bool Wallet::MyRequestKernel2::operator < (const MyRequestKernel2& x) const
    {
        return m_TxID < x.m_TxID;
    }

    bool Wallet::MyRequestTransaction::operator < (const MyRequestTransaction& x) const
    {
        return m_TxID < x.m_TxID;
    }

    bool Wallet::MyRequestUtxoEvents::operator < (const MyRequestUtxoEvents& x) const
    {
        return false;
    }

    void Wallet::RequestHandler::OnComplete(Request& r)
    {
        uint32_t n = get_ParentObj().SyncRemains();

        switch (r.get_Type())
        {
#define THE_MACRO(type, msgOut, msgIn) \
        case Request::Type::type: \
            { \
                MyRequest##type& x = static_cast<MyRequest##type&>(r); \
                get_ParentObj().DeleteReq(x); \
                get_ParentObj().OnRequestComplete(x); \
            } \
            break;

        REQUEST_TYPES_All(THE_MACRO)
#undef THE_MACRO

        default:
            assert(false);
        }

        if (n)
            get_ParentObj().CheckSyncDone();
    }

    void Wallet::confirm_kernel(const TxID& txID, const TxKernel& kernel)
    {
        if (auto it = m_Transactions.find(txID); it != m_Transactions.end())
        {
            MyRequestKernel::Ptr pVal(new MyRequestKernel);
            pVal->m_TxID = txID;
            kernel.get_ID(pVal->m_Msg.m_ID);

            if (PostReqUnique(*pVal))
                LOG_INFO() << txID << " Get proof for kernel: " << pVal->m_Msg.m_ID;
        }
    }

    void Wallet::confirm_kernel(const TxID& txID, const Merkle::Hash& kernelID)
    {
        if (auto it = m_Transactions.find(txID); it != m_Transactions.end())
        {
            MyRequestKernel::Ptr pVal(new MyRequestKernel);
            pVal->m_TxID = txID;
            pVal->m_Msg.m_ID = kernelID;

            if (PostReqUnique(*pVal))
                LOG_INFO() << txID << " Get proof for kernel: " << pVal->m_Msg.m_ID;
        }
    }

    void Wallet::get_kernel(const TxID& txID, const Merkle::Hash& kernelID)
    {
        if (auto it = m_Transactions.find(txID); it != m_Transactions.end())
        {
            MyRequestKernel2::Ptr pVal(new MyRequestKernel2);
            pVal->m_TxID = txID;
            pVal->m_Msg.m_Fetch = true;
            pVal->m_Msg.m_ID = kernelID;

            if (PostReqUnique(*pVal))
            {
                LOG_INFO() << txID << " Get details for kernel: " << pVal->m_Msg.m_ID;
            }
        }
    }

    bool Wallet::get_tip(Block::SystemState::Full& state) const
    {
        return m_WalletDB->get_History().get_Tip(state);
    }

    void Wallet::send_tx_params(const WalletID& peerID, SetTxParameter&& msg)
    {
        m_pWalletNetwork->Send(peerID, std::move(msg));
    }

    void Wallet::OnWalletMessage(const WalletID& myID, wallet::SetTxParameter&& msg)
    {
        auto t = getTransaction(myID, msg);
        if (!t)
        {
            return;
        }
        bool txChanged = false;
        SubTxID subTxID = kDefaultSubTxID;

        for (const auto& p : msg.m_Parameters)
        {
            if (p.first == TxParameterID::SubTxIndex)
            {
                // change subTxID
                Deserializer d;
                d.reset(p.second.data(), p.second.size());
                d & subTxID;
                continue;
            }

            if (p.first < TxParameterID::PrivateFirstParam)
            {
                txChanged |= t->SetParameter(p.first, p.second, subTxID);
            }
            else
            {
                LOG_WARNING() << "Attempt to set private tx parameter";
            }
        }
        if (txChanged)
        {
            updateTransaction(msg.m_TxID);
        }
    }

    void Wallet::OnRequestComplete(MyRequestTransaction& r)
    {
        LOG_DEBUG() << r.m_TxID << (r.m_Res.m_Value ? " has registered" : " has failed to register");
        
        auto it = m_Transactions.find(r.m_TxID);
        if (it != m_Transactions.end())
        {
            it->second->SetRegisteredStatus(r.m_Msg.m_Transaction, r.m_Res.m_Value);
            updateTransaction(r.m_TxID);
        }
    }

    void Wallet::cancel_tx(const TxID& txId)
    {
        LOG_INFO() << txId << "Canceling tx ";

        if (auto it = m_Transactions.find(txId); it != m_Transactions.end())
        {
            it->second->Cancel();
        }
        else
        {
            m_WalletDB->deleteTx(txId);
        }
    }

    void Wallet::delete_tx(const TxID& txId)
    {
        LOG_INFO() << "deleting tx " << txId;
        if (auto it = m_Transactions.find(txId); it == m_Transactions.end())
        {
            m_WalletDB->deleteTx(txId);
        }
        else
        {
            LOG_WARNING() << "Cannot delete running transaction";
        }
    }

    void Wallet::updateTransaction(const TxID& txID)
    {
        auto it = m_Transactions.find(txID);
        if (it != m_Transactions.end())
        {
            auto tx = it->second;
            bool bSynced = !SyncRemains();

            if (bSynced)
                tx->Update();
            else
                m_TransactionsToUpdate.insert(tx);
        }
        else
        {
            LOG_DEBUG() << txID << " Unexpected event";
        }
    }

    void Wallet::OnRequestComplete(MyRequestUtxo& r)
    {
        if (r.m_Res.m_Proofs.empty())
            return; // Right now nothing is concluded from empty proofs

        const auto& proof = r.m_Res.m_Proofs.front(); // Currently - no handling for multiple coins for the same commitment.

        proto::UtxoEvent evt;
        evt.m_Added = 1;
        evt.m_Kidv = r.m_CoinID;
        evt.m_Maturity = proof.m_State.m_Maturity;
        evt.m_Height = MaxHeight; // not used, relevant only for spend events

        ProcessUtxoEvent(evt); // uniform processing for all confirmed utxos
    }

    void Wallet::OnRequestComplete(MyRequestKernel& r)
    {
        auto it = m_Transactions.find(r.m_TxID);
        if (m_Transactions.end() == it)
        {
            return;
        }
        auto tx = it->second;
        if (!r.m_Res.m_Proof.empty())
        {
            m_WalletDB->get_History().AddStates(&r.m_Res.m_Proof.m_State, 1); // why not?

            if (tx->SetParameter(TxParameterID::KernelProofHeight, r.m_Res.m_Proof.m_State.m_Height))
            {
                tx->Update();
            }
        }
        else
        {
            Block::SystemState::Full sTip;
            get_tip(sTip);
            tx->SetParameter(TxParameterID::KernelUnconfirmedHeight, sTip.m_Height);
        }
    }

    void Wallet::OnRequestComplete(MyRequestKernel2 & r)
    {
        auto it = m_Transactions.find(r.m_TxID);
        if (m_Transactions.end() == it)
        {
            return;
        }
        auto tx = it->second;

        if (r.m_Res.m_Kernel && r.m_Res.m_Kernel->m_pHashLock)
        {
            tx->SetParameter(TxParameterID::PreImage, r.m_Res.m_Kernel->m_pHashLock->m_Preimage);
            tx->SetParameter(TxParameterID::KernelProofHeight, r.m_Res.m_Height);
        }
    }

    void Wallet::OnRequestComplete(MyRequestBbsMsg& r)
    {
        assert(false);
    }

    void Wallet::RequestUtxoEvents()
    {
        if (!m_OwnedNodesOnline)
            return;

        Block::SystemState::Full sTip;
        m_WalletDB->get_History().get_Tip(sTip);

        Height h = GetUtxoEventsHeightNext();
        assert(h <= sTip.m_Height + 1);
        if (h > sTip.m_Height)
            return;

        if (!m_PendingUtxoEvents.empty())
        {
            if (m_PendingUtxoEvents.begin()->m_Msg.m_HeightMin == h)
                return; // already pending
            DeleteReq(*m_PendingUtxoEvents.begin());
        }

        MyRequestUtxoEvents::Ptr pReq(new MyRequestUtxoEvents);
        pReq->m_Msg.m_HeightMin = h;
        PostReqUnique(*pReq);
    }

    void Wallet::AbortUtxoEvents()
    {
        if (!m_PendingUtxoEvents.empty())
            DeleteReq(*m_PendingUtxoEvents.begin());
    }

    void Wallet::OnRequestComplete(MyRequestUtxoEvents& r)
    {
        const std::vector<proto::UtxoEvent>& v = r.m_Res.m_Events;
		for (size_t i = 0; i < v.size(); i++)
		{
			const proto::UtxoEvent& evt = v[i];

			// filter-out false positives
			Scalar::Native sk;
			Point comm;
			m_WalletDB->calcCommitment(sk, comm, evt.m_Kidv);

			if (comm == evt.m_Commitment)
				ProcessUtxoEvent(evt);
		}

		if (r.m_Res.m_Events.size() < proto::UtxoEvent::s_Max)
		{
			Block::SystemState::Full sTip;
			m_WalletDB->get_History().get_Tip(sTip);

			SetUtxoEventsHeight(sTip.m_Height);
		}
        else
        {
            SetUtxoEventsHeight(r.m_Res.m_Events.back().m_Height);
            RequestUtxoEvents(); // maybe more events pending
        }
    }

    void Wallet::SetUtxoEventsHeight(Height h)
    {
        uintBigFor<Height>::Type var;
        var = h + 1; // we're actually saving the next
        wallet::setVar(*m_WalletDB, s_szNextUtxoEvt, var);
    }

    Height Wallet::GetUtxoEventsHeightNext()
    {
        uintBigFor<Height>::Type var;
        if (!wallet::getVar(*m_WalletDB, s_szNextUtxoEvt, var))
            return 0;

        Height h;
        var.Export(h);
        return h;
    }

    void Wallet::ProcessUtxoEvent(const proto::UtxoEvent& evt)
    {
        Coin c;
        c.m_ID = evt.m_Kidv;

        bool bExists = m_WalletDB->find(c);
		c.m_maturity = evt.m_Maturity;

        LOG_INFO() << "CoinID: " << evt.m_Kidv << " Maturity=" << evt.m_Maturity << (evt.m_Added ? " Confirmed" : " Spent");

        if (evt.m_Added)
			c.m_confirmHeight = std::min(c.m_confirmHeight, evt.m_Height); // in case of std utxo proofs - the event height may be bigger than actual utxo height
        else
        {
            if (!bExists)
                return; // should alert!

			c.m_spentHeight = std::min(c.m_spentHeight, evt.m_Height); // reported spend height may be bigger than it actuall was (in case of macroblocks)
		}

        m_WalletDB->save(c);
    }

    void Wallet::OnRolledBack()
    {
        Block::SystemState::Full sTip;
        m_WalletDB->get_History().get_Tip(sTip);

        Block::SystemState::ID id;
        sTip.get_ID(id);
        LOG_INFO() << "Rolled back to " << id;

        m_WalletDB->get_History().DeleteFrom(sTip.m_Height + 1);

        m_WalletDB->rollbackConfirmedUtxo(sTip.m_Height);

        ResumeAllTransactions();

        for (auto it = m_Transactions.begin(); m_Transactions.end() != it; it++)
        {
            const auto& pTx = it->second;

            Height h;
            if (pTx->GetParameter(TxParameterID::KernelProofHeight, h) && (h > sTip.m_Height))
            {
                h = 0;
                pTx->SetParameter(TxParameterID::KernelProofHeight, h);
                m_TransactionsToUpdate.insert(pTx);
            }
        }

        Height h = GetUtxoEventsHeightNext();
        if (h > sTip.m_Height + 1)
            SetUtxoEventsHeight(sTip.m_Height);
    }

    void Wallet::OnNewTip()
    {
        m_WalletDB->ShrinkHistory();

        Block::SystemState::Full sTip;
        get_tip(sTip);
        if (!sTip.m_Height)
            return; //?!

        Block::SystemState::ID id;
        sTip.get_ID(id);
        LOG_INFO() << "Sync up to " << id;

        RequestUtxoEvents();

        auto t = m_Transactions;
        for (auto& p : t)
        {
            auto tx = p.second;
            tx->Update();
        }

        CheckSyncDone();
    }

    void Wallet::OnTipUnchanged()
    {
        LOG_INFO() << "Tip has not been changed";
        notifySyncProgress();
    }

    void Wallet::getUtxoProof(const Coin::ID& cid)
    {
        MyRequestUtxo::Ptr pReq(new MyRequestUtxo);
        pReq->m_CoinID = cid;

		Scalar::Native sk;
		m_WalletDB->calcCommitment(sk, pReq->m_Msg.m_Utxo, cid);

        LOG_DEBUG() << "Get utxo proof: " << pReq->m_Msg.m_Utxo;

        PostReqUnique(*pReq);
    }

    uint32_t Wallet::SyncRemains() const
    {
        size_t val =
#define THE_MACRO(type) m_Pending##type.size() +
            REQUEST_TYPES_Sync(THE_MACRO)
#undef THE_MACRO
            0;

        return static_cast<uint32_t>(val);
    }

    void Wallet::CheckSyncDone()
    {
        report_sync_progress();

        if (SyncRemains())
            return;

        m_LastSyncTotal = 0;

        saveKnownState();
    }

    void Wallet::saveKnownState()
    {
        Block::SystemState::Full sTip;
        get_tip(sTip);

        Block::SystemState::ID id;
        if (sTip.m_Height)
            sTip.get_ID(id);
        else
            ZeroObject(id);

        m_WalletDB->setSystemStateID(id);
        LOG_INFO() << "Current state is " << id;
        notifySyncProgress();

        std::unordered_set<wallet::BaseTransaction::Ptr> txSet;
        txSet.swap(m_TransactionsToUpdate);

        for (auto it = txSet.begin(); txSet.end() != it; it++)
        {
            wallet::BaseTransaction::Ptr pTx = *it;
            if (m_Transactions.find(pTx->GetTxID()) != m_Transactions.end())
                pTx->Update();
        }
    }

    void Wallet::notifySyncProgress()
    {
        uint32_t n = SyncRemains();
        for (auto sub : m_subscribers)
        {
            sub->onSyncProgress(m_LastSyncTotal - n, m_LastSyncTotal);
        }
    }

    void Wallet::report_sync_progress()
    {
        if (!m_LastSyncTotal)
            return;

        uint32_t nDone = m_LastSyncTotal - SyncRemains();
        assert(nDone <= m_LastSyncTotal);
        int p = static_cast<int>((nDone * 100) / m_LastSyncTotal);
        LOG_INFO() << "Synchronizing with node: " << p << "% (" << nDone << "/" << m_LastSyncTotal << ")";

        notifySyncProgress();
    }

    void Wallet::register_tx(const TxID& txId, Transaction::Ptr data)
    {
        LOG_VERBOSE() << txId << " sending tx for registration";

#ifndef NDEBUG
        TxBase::Context ctx;
        assert(data->IsValid(ctx));
#endif // NDEBUG

        MyRequestTransaction::Ptr pReq(new MyRequestTransaction);
        pReq->m_TxID = txId;
        pReq->m_Msg.m_Transaction = std::move(data);

        PostReqUnique(*pReq);
    }

    BitcoinRPC::Ptr Wallet::get_bitcoin_rpc() const
    {
        return m_bitcoinRPC;
    }

    void Wallet::subscribe(IWalletObserver* observer)
    {
        assert(std::find(m_subscribers.begin(), m_subscribers.end(), observer) == m_subscribers.end());

        m_subscribers.push_back(observer);

        m_WalletDB->subscribe(observer);
    }

    void Wallet::unsubscribe(IWalletObserver* observer)
    {
        auto it = std::find(m_subscribers.begin(), m_subscribers.end(), observer);

        assert(it != m_subscribers.end());

        m_subscribers.erase(it);

        m_WalletDB->unsubscribe(observer);
    }

    wallet::BaseTransaction::Ptr Wallet::getTransaction(const WalletID& myID, const wallet::SetTxParameter& msg)
    {
        auto it = m_Transactions.find(msg.m_TxID);
        if (it != m_Transactions.end())
        {
            if (it->second->GetType() != msg.m_Type)
            {
                LOG_WARNING() << msg.m_TxID << " Parameters for invalid tx type";
            }
            return it->second;
        }

        TxType type = TxType::Simple;
        if (wallet::getTxParameter(*m_WalletDB, msg.m_TxID, TxParameterID::TransactionType, type))
        {
            // we return only active transactions
            return BaseTransaction::Ptr();
        }

        bool isSender = false;
        if (!msg.GetParameter(TxParameterID::IsSender, isSender) || isSender == true)
        {
            return BaseTransaction::Ptr();
        }

        auto t = constructTransaction(msg.m_TxID, msg.m_Type);

        t->SetParameter(TxParameterID::TransactionType, msg.m_Type, false);
        t->SetParameter(TxParameterID::CreateTime, getTimestamp(), false);
        t->SetParameter(TxParameterID::MyID, myID, false);
        t->SetParameter(TxParameterID::PeerID, msg.m_From, false);
        t->SetParameter(TxParameterID::IsInitiator, false, false);
        t->SetParameter(TxParameterID::Status, TxStatus::Pending, true);

        auto address = m_WalletDB->getAddress(myID);
        if (address.is_initialized())
        {
            ByteBuffer message(address->m_label.begin(), address->m_label.end());
            t->SetParameter(TxParameterID::Message, message);
        }

        m_Transactions.emplace(msg.m_TxID, t);
        return t;
    }

    wallet::BaseTransaction::Ptr Wallet::constructTransaction(const TxID& id, TxType type)
    {
        switch (type)
        {
        case TxType::Simple:
             return make_shared<SimpleTransaction>(*this, m_WalletDB, id);
        case TxType::AtomicSwap:
            return make_shared<AtomicSwapTransaction>(*this, m_WalletDB, id);
        }
        return wallet::BaseTransaction::Ptr();
    }
}
