#pragma once

#include "core/common.h"
#include "core/ecc_native.h"
#include <boost/msm/back/state_machine.hpp>
#include <boost/msm/front/state_machine_def.hpp>
#include "core/serialization_adapters.h"

namespace beam
{
    using Uuid = std::array<uint8_t, 16>;

    std::ostream& operator<<(std::ostream& os, const Uuid& uuid);
    struct Coin;
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
            bool process_event(const Event& event)
            {
                return static_cast<Derived*>(this)->m_fsm.process_event(event) == msm::back::HANDLED_TRUE;
            }
        };

        struct TxRegisteredData
        {
            bool m_value;
            SERIALIZE(m_value);
        };

        struct IWalletGateway
        {
            virtual ~IWalletGateway() {}
            virtual void on_tx_completed(const Uuid& txId) = 0;
            virtual void send_output_confirmation(const Uuid& txId, const Coin& coin) = 0;
            virtual void send_tx_failed(const Uuid& txId) = 0;
        };

        namespace sender
        {
            // interface to communicate with receiver
            struct InvitationData
            {
                using Ptr = std::shared_ptr<InvitationData>;

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
                        , m_message
                        , m_publicSenderBlindingExcess
                        , m_publicSenderNonce
                        , m_inputs
                        , m_outputs);
            };

            struct ConfirmationData
            {
                using Ptr = std::shared_ptr<ConfirmationData>;

                Uuid m_txId;
                ECC::Scalar m_senderSignature;

                SERIALIZE(m_txId, m_senderSignature);
            };

            struct IGateway : virtual IWalletGateway
            {
                virtual void send_tx_invitation(InvitationData::Ptr) = 0;
                virtual void send_tx_confirmation(ConfirmationData::Ptr) = 0;
            };
        }

        namespace receiver
        {
            // interface to communicate with sender
            struct ConfirmationData
            {
                using Ptr = std::shared_ptr<ConfirmationData>;

                Uuid m_txId;
                ECC::Point m_publicReceiverBlindingExcess;
                ECC::Point m_publicReceiverNonce;
                ECC::Scalar m_receiverSignature;

                SERIALIZE(m_txId, m_publicReceiverBlindingExcess, m_publicReceiverNonce, m_receiverSignature);
            };

            struct IGateway : virtual IWalletGateway
            {
                virtual void send_tx_confirmation(ConfirmationData::Ptr) = 0;
                virtual void register_tx(const Uuid&, Transaction::Ptr) = 0;
                virtual void send_tx_registered(UuidPtr&&) = 0;
            };
        }
    }
}