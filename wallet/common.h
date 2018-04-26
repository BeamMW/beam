#pragma once

#include "core/common.h"
#include "core/ecc_native.h"
#include <boost/msm/back/state_machine.hpp>
#include <boost/msm/front/state_machine_def.hpp>

namespace beam
{
    using Uuid = std::array<uint8_t, 16>;
    using TransactionPtr = std::shared_ptr<Transaction>;
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
                virtual void sendTxInitiation(InvitationData::Ptr) = 0;
                virtual void sendTxConfirmation(ConfirmationData::Ptr) = 0;
                virtual void sendChangeOutputConfirmation() = 0;
                virtual void removeSender(const Uuid&) = 0;
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

            struct IGateway
            {
                virtual void sendTxConfirmation(ConfirmationData::Ptr) = 0;
                virtual void registerTx(const Uuid& txId, TransactionPtr) = 0;
                virtual void sendTxRegistered(const Uuid& txId) = 0;
                virtual void removeReceiver(const Uuid&) = 0;
            };
        }
    }
}