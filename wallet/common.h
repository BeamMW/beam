#pragma once

#include "core/common.h"
#include "core/ecc_native.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4127 )
#endif

#include <boost/msm/back/state_machine.hpp>
#include <boost/msm/front/state_machine_def.hpp>
#include <boost/msm/front/functor_row.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "core/serialization_adapters.h"

namespace beam
{
    using TxID = std::array<uint8_t, 16>;
    using WalletID = PeerID;

    struct PrintableAmount
    {
        explicit PrintableAmount(const Amount& amount, bool showPoint = false) : m_value{ amount }, m_showPoint{showPoint}
        {}
        const Amount& m_value;
        bool m_showPoint;
    };

    std::ostream& operator<<(std::ostream& os, const PrintableAmount& amount);
    std::ostream& operator<<(std::ostream& os, const TxID& uuid);

    struct Coin;
    
    struct TxDescription
    {
        enum Status
        {
            Pending,
            InProgress,
            Cancelled,
            Completed,
            Failed
        };

        TxDescription() = default;

        TxDescription(const TxID& txId
                    , Amount amount
                    , Amount fee
                    , Height minHeight
                    , const WalletID& peerId
                    , ByteBuffer&& message
                    , Timestamp createTime
                    , bool sender)
            : m_txId{ txId }
            , m_amount{ amount }
            , m_fee{ fee }
			, m_change{}
            , m_minHeight{ minHeight }
            , m_peerId{ peerId }
            , m_message{ std::move(message) }
            , m_createTime{ createTime }
            , m_modifyTime{ createTime }
            , m_sender{ sender }
            , m_status{ Pending }
            , m_fsmState{}
        {}

        TxID m_txId;
        Amount m_amount;
        Amount m_fee;
		Amount m_change;
        Height m_minHeight;
        WalletID m_peerId;
        ByteBuffer m_message;
        Timestamp m_createTime;
        Timestamp m_modifyTime;
        bool m_sender;
        Status m_status;
        ByteBuffer m_fsmState;
    };

    namespace wallet
    {
        namespace msm = boost::msm;
        namespace msmf = boost::msm::front;
        namespace mpl = boost::mpl;

        std::pair<ECC::Scalar::Native, ECC::Scalar::Native> splitKey(const ECC::Scalar::Native& key, uint64_t index);

        // messages
        struct Invite
        {
            TxID m_txId;
            ECC::Amount m_amount;
            ECC::Amount m_fee;
            Height m_height;
            bool m_send;
            ECC::Point m_publicPeerExcess;
            ECC::Scalar m_offset;
            ECC::Point m_publicPeerNonce;
            std::vector<Input::Ptr> m_inputs;
            std::vector<Output::Ptr> m_outputs;

            Invite() 
                : m_amount(0)
                , m_fee(0)
                , m_send{true}
                
            {

            }

            Invite(Invite&& other)
                : m_txId{other.m_txId}
                , m_amount{ other.m_amount }
                , m_fee{ other.m_fee }
                , m_height{other.m_height }
                , m_send{other.m_send}
                , m_publicPeerExcess{other.m_publicPeerExcess}
                , m_offset{other.m_offset}
                , m_publicPeerNonce{other.m_publicPeerNonce}
                , m_inputs{std::move(other.m_inputs)}
                , m_outputs{std::move(other.m_outputs)}
            {

            }

            SERIALIZE(m_txId
                    , m_amount
                    , m_fee
                    , m_height
                    , m_send
                    , m_publicPeerExcess
                    , m_offset
                    , m_publicPeerNonce
                    , m_inputs
                    , m_outputs);
        };

        struct ConfirmTransaction
        {
            TxID m_txId{};
            ECC::Scalar m_peerSignature;

            SERIALIZE(m_txId, m_peerSignature);
        };

        struct ConfirmInvitation
        {
            TxID m_txId{};
            ECC::Point m_publicPeerExcess;
            ECC::Point m_publicPeerNonce;
            ECC::Scalar m_peerSignature;

            SERIALIZE(m_txId
                    , m_publicPeerExcess
                    , m_publicPeerNonce
                    , m_peerSignature);
        };

        struct TxRegistered
        {
            TxID m_txId;
            bool m_value;
            SERIALIZE(m_txId, m_value);
        };

        struct TxFailed
        {
            TxID m_txId;
            SERIALIZE(m_txId);
        };

        struct INegotiatorGateway
        {
            virtual ~INegotiatorGateway() {}
            virtual void on_tx_completed(const TxDescription& ) = 0;
            virtual void send_tx_failed(const TxDescription& ) = 0;
            virtual void send_tx_invitation(const TxDescription&, Invite&&) = 0;
            virtual void send_tx_confirmation(const TxDescription&, ConfirmTransaction&&) = 0;
            virtual void send_tx_confirmation(const TxDescription&, ConfirmInvitation&&) = 0;
            virtual void register_tx(const TxDescription&, Transaction::Ptr) = 0;
            virtual void send_tx_registered(const TxDescription&) = 0;
        };
    }
}

namespace std
{
    string to_string(const beam::WalletID&);
}
