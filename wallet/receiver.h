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

        Receiver(receiver::IGateway& gateway
               , IKeyChain::Ptr keychain
               , const TxDescription& txDesc
               , InviteReceiver& initData)
            : m_txDesc{txDesc}
            , m_fsm{std::ref(gateway), keychain, std::ref(m_txDesc), std::ref(initData)}
        {
            assert(keychain);
        }

        struct FSMDefinition : public msmf::state_machine_def<FSMDefinition>
                             , public FSMDefinitionBase<FSMDefinition>
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
                    fsm.m_gateway.on_tx_completed(fsm.m_txDesc);
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

            FSMDefinition(receiver::IGateway &gateway, beam::IKeyChain::Ptr keychain, TxDescription& txDesc, InviteReceiver& initData);

            // transition actions
            void confirm_tx(const msmf::none&);
            bool is_valid_signature(const events::TxConfirmationCompleted2& event);
            bool is_invalid_signature(const events::TxConfirmationCompleted2& event);
            void register_tx(const events::TxConfirmationCompleted2& event);
            void rollback_tx(const events::TxFailed& event);
            void cancel_tx(const events::TxConfirmationCompleted2& event);
            void complete_tx(const events::TxRegistrationCompleted& event);
            void rollback_tx();

            void update_tx_description(TxDescription::Status s);

            using do_serialize = int;
            using initial_state = Init;
            using d = FSMDefinition;
            struct transition_table : mpl::vector<
                //   Start                 Event                              Next                   Action               Guard
                a_row< Init              , msmf::none                       , TxConfirming         , &d::confirm_tx                               >,
                a_row< Init              , events::TxFailed                 , Terminate            , &d::rollback_tx                              >,
                a_row< TxConfirming      , events::TxFailed                 , Terminate            , &d::rollback_tx                              >,
                row  < TxConfirming      , events::TxConfirmationCompleted2 , TxRegistering        , &d::register_tx    , &d::is_valid_signature  >,
                row  < TxConfirming      , events::TxConfirmationCompleted2 , Terminate            , &d::cancel_tx      , &d::is_invalid_signature>,
                a_row< TxRegistering     , events::TxRegistrationCompleted  , Terminate            , &d::complete_tx                              >,
                a_row< TxRegistering     , events::TxFailed                 , Terminate            , &d::rollback_tx                              >
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
                ar  & m_publicSenderBlindingExcess
                    & m_publicSenderNonce
                    & m_blindingExcess
                    & m_transaction
                    & m_kernel;
            }

            receiver::IGateway& m_gateway;
            beam::IKeyChain::Ptr m_keychain;

            ECC::Hash::Value m_message;

            ECC::Point::Native m_publicSenderBlindingExcess;
            ECC::Point::Native m_publicSenderNonce;
            ECC::Scalar::Native m_blindingExcess;

            Transaction::Ptr m_transaction;
            TxKernel::Ptr m_kernel;
        };
    private:
        friend FSMHelper<Receiver>;
        TxDescription m_txDesc;
        msm::back::state_machine<FSMDefinition> m_fsm;

    };
}
