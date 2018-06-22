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
    using Uuid = std::array<uint8_t, 16>;

    struct PrintableAmount
    {
        explicit PrintableAmount(const Amount& amount) : m_value{amount}
        {}
        const Amount& m_value;
    };

    std::ostream& operator<<(std::ostream& os, const PrintableAmount& amount);
    std::ostream& operator<<(std::ostream& os, const Uuid& uuid);

    struct Coin;
    using TransactionPtr = std::shared_ptr<Transaction>;
    
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

        TxDescription(const Uuid& txId
            , Amount amount
            , uint64_t peerId
            , ByteBuffer&& message
            , Timestamp createTime
            , bool sender)
            : m_txId{ txId }
            , m_amount{ amount }
            , m_peerId{ peerId }
            , m_message{ std::move(message) }
            , m_createTime{ createTime }
            , m_modifyTime{ createTime }
            , m_sender{ sender }
            , m_status{ Pending }
            , m_fsmState{}
        {}

        Uuid m_txId;
        Amount m_amount;
        uint64_t m_peerId;
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
        Timestamp getTimestamp();

        template <typename Derived>
        class FSMHelper 
        {
        public:
            void start()
            {
                static_cast<Derived*>(this)->m_fsm.start();
            }

            template<typename Event>
            bool process_event(const Event& event)
            {
                auto* d = static_cast<Derived*>(this);
                auto res = d->m_fsm.process_event(event) == msm::back::HANDLED_TRUE;
                return res;
            }

            template<class Archive>
            void serialize(Archive & ar, const unsigned int)
            {
                static_cast<Derived*>(this)->m_fsm.serialize(ar, 0);
            }

            // for test only
            const int* current_state() const
            {
                return static_cast<const Derived*>(this)->m_fsm.current_state();
            }
        };

        template <typename Derived>
        struct FSMDefinitionBase 
        {
            FSMDefinitionBase(TxDescription& txDesc) : m_txDesc{txDesc}
            {}
            
            TxDescription & m_txDesc;
        };

        // messages
        struct InviteReceiver
        {
            Uuid m_txId;
            ECC::Amount m_amount;
            Height m_height;
            ECC::Hash::Value m_message;
            ECC::Point m_publicSenderBlindingExcess;
            ECC::Point m_publicSenderNonce;
            std::vector<Input::Ptr> m_inputs;
            std::vector<Output::Ptr> m_outputs;

            SERIALIZE(m_txId
                    , m_amount
                    , m_height
                    , m_message
                    , m_publicSenderBlindingExcess
                    , m_publicSenderNonce
                    , m_inputs
                    , m_outputs);
        };

        struct ConfirmTransaction
        {
            Uuid m_txId;
            ECC::Scalar m_senderSignature;

            SERIALIZE(m_txId, m_senderSignature);
        };

        struct ConfirmInvitation
        {
            Uuid m_txId{};
            ECC::Point m_publicReceiverBlindingExcess;
            ECC::Point m_publicReceiverNonce;
            ECC::Scalar m_receiverSignature;

            SERIALIZE(m_txId
                    , m_publicReceiverBlindingExcess
                    , m_publicReceiverNonce
                    , m_receiverSignature);
        };

        struct TxRegistered
        {
            Uuid m_txId;
            bool m_value;
            SERIALIZE(m_txId, m_value);
        };

        struct TxFailed
        {
            Uuid m_txId;
            SERIALIZE(m_txId);
        };

        struct IWalletGateway
        {
            virtual ~IWalletGateway() {}
            virtual void on_tx_completed(const TxDescription& ) = 0;
            virtual void send_tx_failed(const TxDescription& ) = 0;
        };

        namespace sender
        {
            struct IGateway : virtual IWalletGateway
            {
                virtual void send_tx_invitation(const TxDescription&, const InviteReceiver&) = 0;
                virtual void send_tx_confirmation(const TxDescription& , const ConfirmTransaction&) = 0;
            };
        }

        namespace receiver
        {
            struct IGateway : virtual IWalletGateway
            {
                virtual void send_tx_confirmation(const TxDescription& , const ConfirmInvitation&) = 0;
                virtual void register_tx(const TxDescription& , Transaction::Ptr) = 0;
                virtual void send_tx_registered(const TxDescription& ) = 0;
            };
        }
    }
}