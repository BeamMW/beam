#pragma once

#include "wallet/wallet_db.h"
#include "wallet/negotiator.h"
#include <deque>
#include "core/proto.h"

namespace beam
{
    struct IWallet
    {
        using Ptr = std::shared_ptr<IWallet>;
        virtual ~IWallet() {}
        // wallet to wallet responses
        virtual void handle_tx_message(const WalletID&, wallet::Invite&&) = 0;
        virtual void handle_tx_message(const WalletID&, wallet::ConfirmTransaction&&) = 0;
        virtual void handle_tx_message(const WalletID&, wallet::ConfirmInvitation&&) = 0;
        virtual void handle_tx_message(const WalletID&, wallet::TxRegistered&&) = 0;
        virtual void handle_tx_message(const WalletID&, wallet::TxFailed&&) = 0;
        // node to wallet responses
        virtual bool handle_node_message(proto::Boolean&&) = 0;
        virtual bool handle_node_message(proto::ProofUtxo&&) = 0;
        virtual bool handle_node_message(proto::NewTip&&) = 0;
        virtual bool handle_node_message(proto::Hdr&&) = 0;
        virtual bool handle_node_message(proto::Mined&& msg) = 0;
        virtual bool handle_node_message(proto::Proof&& msg) = 0;

        virtual void stop_sync() = 0;
    };

    struct INetworkIO 
    {
        using Ptr = std::shared_ptr<INetworkIO>;
        virtual ~INetworkIO() {}
        virtual void set_wallet(IWallet*) = 0;
        // wallet to wallet requests
        virtual void send_tx_message(const WalletID& to, wallet::Invite&&) = 0;
        virtual void send_tx_message(const WalletID& to, wallet::ConfirmTransaction&&) = 0;
        virtual void send_tx_message(const WalletID& to, wallet::ConfirmInvitation&&) = 0;
        virtual void send_tx_message(const WalletID& to, wallet::TxRegistered&&) = 0 ;
        virtual void send_tx_message(const WalletID& to, wallet::TxFailed&&) = 0;
        // wallet to node requests
        virtual void send_node_message(proto::NewTransaction&&) = 0;
        virtual void send_node_message(proto::GetProofUtxo&&) = 0;
		virtual void send_node_message(proto::GetHdr&&) = 0;
        virtual void send_node_message(proto::GetMined&&) = 0;
        virtual void send_node_message(proto::GetProofState&&) = 0;
        // connection control
        virtual void close_connection(const WalletID& id) = 0;
        virtual void connect_node() = 0;
        virtual void close_node_connection() = 0;
    };

    class NetworkIOBase : public INetworkIO
    {
    protected:
        IWallet& get_wallet() const
        {
            assert(m_wallet);
            return *m_wallet;
        }
    private:
        void set_wallet(IWallet* wallet) override
        {
            m_wallet = wallet;
            if (wallet != nullptr)
            {
                connect_node();
            }
        }
        IWallet* m_wallet; // wallet holds reference to INetworkIO
    };

    class Wallet : public IWallet
                 , public wallet::INegotiatorGateway
    {
        using Callback = std::function<void()>;
    public:
        using TxCompletedAction = std::function<void(const TxID& tx_id)>;

        Wallet(IKeyChain::Ptr keyChain, INetworkIO::Ptr network, TxCompletedAction&& action = TxCompletedAction());
        virtual ~Wallet();

        TxID transfer_money(const WalletID& to, Amount amount, Amount fee = 0, bool sender = true, ByteBuffer&& message = {} );
        void resume_tx(const TxDescription& tx);
        void resume_all_tx();

        void send_tx_invitation(const TxDescription& tx, wallet::Invite&&) override;
        void send_tx_confirmation(const TxDescription& tx, wallet::ConfirmTransaction&&) override;
        void on_tx_completed(const TxDescription& tx) override;
        void send_tx_failed(const TxDescription& tx) override;

        void send_tx_confirmation(const TxDescription& tx, wallet::ConfirmInvitation&&) override;
        void register_tx(const TxDescription& tx, Transaction::Ptr) override;
        void send_tx_registered(const TxDescription& tx) override;

        void handle_tx_message(const WalletID&, wallet::Invite&&) override;
        void handle_tx_message(const WalletID&, wallet::ConfirmTransaction&&) override;
        void handle_tx_message(const WalletID&, wallet::ConfirmInvitation&&) override;
        void handle_tx_message(const WalletID&, wallet::TxRegistered&&) override;
        void handle_tx_message(const WalletID&, wallet::TxFailed&&) override;

        bool handle_node_message(proto::Boolean&& res) override;
        bool handle_node_message(proto::ProofUtxo&& proof) override;
        bool handle_node_message(proto::NewTip&& msg) override;
        bool handle_node_message(proto::Hdr&& msg) override;
        bool handle_node_message(proto::Mined&& msg) override;
        bool handle_node_message(proto::Proof&& msg) override;

        void stop_sync() override;

        void handle_tx_registered(const TxID& txId, bool res);
        void handle_tx_failed(const TxID& txId);

    private:
        void remove_peer(const TxID& txId);
        void getUtxoProofs(const std::vector<Coin>& coins);
        void do_fast_forward();
        bool finish_sync();
        bool close_node_connection();
        void register_tx(const TxID& txId, Transaction::Ptr);
        void resume_negotiator(const TxDescription& tx);

        struct Cleaner
        {
            Cleaner(std::vector<wallet::Negotiator::Ptr>& t) : m_v{ t } {}
            ~Cleaner()
            {
                if (!m_v.empty())
                {
                    m_v.clear();
                }
            }
            std::vector<wallet::Negotiator::Ptr>& m_v;
        };

        template<typename Event>
        bool process_event(const TxID& txId, Event&& event)
        {
            Cleaner cs{ m_removedNegotiators };
            if (auto it = m_negotiators.find(txId); it != m_negotiators.end())
            {
                return it->second->process_event(event);
            }
            return false;
        }

    private:

        struct StateFinder;

        IKeyChain::Ptr m_keyChain;
        INetworkIO::Ptr m_network;
        std::map<WalletID, wallet::Negotiator::Ptr> m_peers;
        std::map<TxID, wallet::Negotiator::Ptr>   m_negotiators;
        std::vector<wallet::Negotiator::Ptr>      m_removedNegotiators;
        TxCompletedAction m_tx_completed_action;
        std::deque<std::pair<TxID, Transaction::Ptr>> m_reg_requests;
        std::vector<std::pair<TxID, Transaction::Ptr>> m_pending_reg_requests;
        std::deque<Coin> m_pendingProofs;
        std::vector<Callback> m_pendingEvents;

        Merkle::Hash m_Definition;
        Block::SystemState::ID m_knownStateID;
        Block::SystemState::ID m_newStateID;
        std::unique_ptr<StateFinder> m_stateFinder;

        int m_syncing;
        bool m_synchronized;
    };
}
