#pragma once

#include "wallet/common.h"
#include "wallet/wallet_db.h"
#include "wallet/sender.h"

namespace beam::wallet
{
    class Receiver : public FSMHelper<Receiver>
    {
    public:
        using Ptr = std::shared_ptr<Receiver>;
        // events
        struct TxFailed {};
        struct TxConfirmationCompleted
        {
            ConfirmTransaction data;
        };
        struct TxRegistrationCompleted 
        {
            Uuid m_txId;
        };
        
        Receiver(receiver::IGateway& gateway, beam::IKeyChain::Ptr keychain, InviteReceiver& initData)
            : m_fsm{std::ref(gateway), keychain, std::ref(initData)}
        {
            assert(keychain);
        }  

        void update_history();

        struct FSMDefinition : public msmf::state_machine_def<FSMDefinition>
        {
            // states
            struct Init : public msmf::state<>
            {
                template <class Event, class Fsm>
                void on_entry(Event const&, Fsm&)
                {
                    LOG_VERBOSE() << "[Receiver] Init state";
                }
            };
            struct Terminate : public msmf::terminate_state<> {
                template <class Event, class Fsm>
                void on_entry(Event const&, Fsm& fsm)
                {
                    LOG_VERBOSE() << "[Receiver] Terminate state";
                    fsm.m_gateway.on_tx_completed(fsm.m_txId);
                }
            };
            struct TxConfirming : public msmf::state<>
            {
                template <class Event, class Fsm>
                void on_entry(Event const&, Fsm&)
                {
                    LOG_VERBOSE() << "[Receiver] TxConfirming state";
                }
            };
            struct TxRegistering : public msmf::state<>
            {
                template <class Event, class Fsm>
                void on_entry(Event const&, Fsm&)
                {
                    LOG_VERBOSE() << "[Receiver] TxRegistering state";
                }
            };
            struct TxOutputConfirming : public msmf::state<>
            {
                template <class Event, class Fsm>
                void on_entry(Event const&, Fsm&)
                {
                    LOG_VERBOSE() << "[Receiver] TxOutputConfirming state";
                }
            };

            FSMDefinition(receiver::IGateway &gateway, beam::IKeyChain::Ptr keychain, InviteReceiver& initData);

            // transition actions
            void confirm_tx(const msmf::none&);
            bool is_valid_signature(const TxConfirmationCompleted& event);
            bool is_invalid_signature(const TxConfirmationCompleted& event);
            void register_tx(const TxConfirmationCompleted& event);
            void rollback_tx(const TxFailed& event);
            void cancel_tx(const TxConfirmationCompleted& event);
            void complete_tx(const TxRegistrationCompleted& event);
            void rollback_tx();

            using do_serialize = int;
            using initial_state = Init;
            using d = FSMDefinition;
            struct transition_table : mpl::vector<
                //   Start                 Event                     Next                   Action               Guard
                a_row< Init              , msmf::none              , TxConfirming         , &d::confirm_tx                               >,
                a_row< Init              , TxFailed                , Terminate            , &d::rollback_tx                              >,
                a_row< TxConfirming      , TxFailed                , Terminate            , &d::rollback_tx                              >,
                row  < TxConfirming      , TxConfirmationCompleted , TxRegistering        , &d::register_tx    , &d::is_valid_signature  >,
                row  < TxConfirming      , TxConfirmationCompleted , Terminate            , &d::cancel_tx      , &d::is_invalid_signature>,
                a_row< TxRegistering     , TxRegistrationCompleted , Terminate			  , &d::complete_tx								 >,
                a_row< TxRegistering     , TxFailed                , Terminate            , &d::rollback_tx                              >
            > {};

            template <class FSM, class Event>
            void no_transition(Event const& e, FSM&, int state)
            {
                LOG_DEBUG() << "[Receiver]: no transition from state " << state
                            << " on event " << typeid(e).name();
            }

            template <class FSM, class Event>
            void exception_caught(Event const&, FSM& fsm, std::exception& ex)
            {
                LOG_ERROR() << ex.what();
                fsm.process_event(TxFailed());
            }

            template<typename Archive>
            void serialize(Archive & ar, const unsigned int)
            {
                ar  & m_txId
                    & m_amount
                    & m_message
                    & m_receiver_coin
                    & m_publicReceiverBlindingExcess
                    & m_publicSenderBlindingExcess
                    & m_publicSenderNonce
                    & m_receiverSignature
                    & m_blindingExcess
                    & m_transaction
                    & m_kernel
                    & m_height;
            }

            receiver::IGateway& m_gateway;
            beam::IKeyChain::Ptr m_keychain;

            Uuid m_txId;

            ECC::Amount m_amount; 
            ECC::Hash::Value m_message;
            Coin m_receiver_coin;

            ECC::Point::Native m_publicReceiverBlindingExcess;
            ECC::Point::Native m_publicSenderBlindingExcess;
            ECC::Point::Native m_publicSenderNonce;
            ECC::Scalar::Native m_receiverSignature;
            ECC::Scalar::Native m_blindingExcess;

            Transaction::Ptr m_transaction;
            TxKernel::Ptr m_kernel;
            Height m_height;
        };

        friend FSMHelper<Receiver>;
        msm::back::state_machine<FSMDefinition> m_fsm;

    };
}