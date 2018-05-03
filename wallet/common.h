#pragma once

#include "core/common.h"
#include "core/ecc_native.h"
#include <boost/msm/back/state_machine.hpp>
#include <boost/msm/front/state_machine_def.hpp>

namespace beam
{
    using Uuid = std::array<uint8_t, 16>;
    using UuidPtr = std::shared_ptr<Uuid>;
    using TransactionPtr = std::shared_ptr<Transaction>;
    ECC::Scalar::Native generateNonce();
    namespace wallet
    {
        namespace msm = boost::msm;
        namespace msmf = boost::msm::front;
        namespace mpl = boost::mpl;

        template <typename Derived>
        class FSMHelper 
        {
        public:
            void start()
            {
                static_cast<Derived*>(this)->m_fsm.start();
            }

            template<typename Event>
            bool processEvent(const Event& event)
            {
                return static_cast<Derived*>(this)->m_fsm.process_event(event) == msm::back::HANDLED_TRUE;
            }
        };

        namespace sender
        {
            // interface to communicate with receiver
            struct InvitationData
            {
                using Ptr = std::shared_ptr<InvitationData>;

                Uuid m_txId;
                ECC::Amount m_amount; ///??
                ECC::Hash::Value m_message;
                ECC::Point::Native m_publicSenderBlindingExcess;
                ECC::Point::Native m_publicSenderNonce;
                std::vector<Input::Ptr> m_inputs;
                std::vector<Output::Ptr> m_outputs;
            };

            struct ConfirmationData
            {
                using Ptr = std::shared_ptr<ConfirmationData>;

                Uuid m_txId;
                ECC::Scalar::Native m_senderSignature;
            };

            struct IGateway
            {
                virtual void send_tx_invitation(InvitationData::Ptr) = 0;
                virtual void send_tx_confirmation(ConfirmationData::Ptr) = 0;
                virtual void sendChangeOutputConfirmation() = 0;
                virtual void remove_sender(const Uuid&) = 0;
            };
        }

        namespace receiver
        {
            // interface to communicate with sender
            struct ConfirmationData
            {
                using Ptr = std::shared_ptr<ConfirmationData>;

                Uuid m_txId;
                ECC::Point::Native m_publicReceiverBlindingExcess;
                ECC::Point::Native m_publicReceiverNonce;
                ECC::Scalar::Native m_receiverSignature;
            };

            struct RegisterTxData
            {
                using Ptr = std::shared_ptr<RegisterTxData>;

                Uuid m_txId;
                TransactionPtr m_transaction;
            };

            struct IGateway
            {
                virtual void send_tx_confirmation(ConfirmationData::Ptr) = 0;
                virtual void register_tx(RegisterTxData::Ptr) = 0;
                virtual void send_tx_registered(UuidPtr&&) = 0;
                virtual void remove_receiver(const Uuid&) = 0;
            };
        }
    }
}