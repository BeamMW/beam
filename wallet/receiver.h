#pragma once

#include "wallet/common.h"
#include "wallet/keychain.h"
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
            sender::ConfirmationData data;
        };
        struct TxRegistrationCompleted 
        {
            Uuid m_txId;
        };
        
        Receiver(receiver::IGateway& gateway, beam::IKeyChain::Ptr keychain, sender::InvitationData& initData)
            : m_fsm{std::ref(gateway), keychain, std::ref(initData)}
        {
            assert(keychain);
        }  
    private:
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

            FSMDefinition(receiver::IGateway &gateway, beam::IKeyChain::Ptr keychain, sender::InvitationData& initData);

            // transition actions
            void confirm_tx(const msmf::none&);
            bool is_valid_signature(const TxConfirmationCompleted& event);
            bool is_invalid_signature(const TxConfirmationCompleted& event);
            void register_tx(const TxConfirmationCompleted& event);
            void rollback_tx(const TxFailed& event);
            void cancel_tx(const TxConfirmationCompleted& event);
            void complete_tx(const TxRegistrationCompleted& event);

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

            receiver::IGateway& m_gateway;
            beam::IKeyChain::Ptr m_keychain;

            Uuid m_txId;

            ECC::Amount m_amount; 
            ECC::Hash::Value m_message;
            std::vector<Input::Ptr> m_inputs;
            std::vector<Output::Ptr> m_outputs;
            Coin m_receiver_coin;

            ECC::Point::Native m_publicReceiverBlindingExcess;
            ECC::Point::Native m_publicSenderBlindingExcess;
            ECC::Point::Native m_publicSenderNonce;
            ECC::Scalar::Native m_receiverSignature;
            ECC::Scalar::Native m_blindingExcess;
            ECC::Scalar::Native m_nonce;
            ECC::Scalar::Native m_schnorrChallenge;

            Transaction::Ptr m_transaction;
            TxKernel* m_kernel;
            Height m_height;
        };

        friend FSMHelper<Receiver>;
        msm::back::state_machine<FSMDefinition> m_fsm;

    };
}