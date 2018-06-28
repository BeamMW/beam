#pragma once

#include "wallet/common.h"
#include "wallet/wallet_db.h"

#include <boost/optional.hpp>
#include "utility/logger.h"

namespace beam::wallet
{
    class Sender : public FSMHelper<Sender>
    {
    public:
        using Ptr = std::shared_ptr<Sender>;
        // events
        struct TxFailed {};
        struct TxInitCompleted
        {
            ConfirmInvitation data;
        };
        struct TxConfirmationCompleted {};

        Sender(sender::IGateway& gateway
             , beam::IKeyChain::Ptr keychain
             , const TxDescription& txDesc )
            : m_txDesc{txDesc}
            , m_fsm{boost::ref(gateway), keychain, boost::ref(m_txDesc)}
        {

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
                    LOG_VERBOSE() << "[Sender] Init state";
                }
            };
            struct Terminate : public msmf::terminate_state<>
            {
                template <class Event, class Fsm>
                void on_entry(Event const&, Fsm& fsm)
                {
                    LOG_VERBOSE() << "[Sender] Terminate state";
                    fsm.m_gateway.on_tx_completed(fsm.m_txDesc);
                }
            };
            struct TxInitiating : public msmf::state<>
            {
                template <class Event, class Fsm>
                void on_entry(Event const&, Fsm&)
                {
                    LOG_VERBOSE() << "[Sender] TxInitiating state";
                }
            };
            struct TxConfirming : public msmf::state<>
            {
                template <class Event, class Fsm>
                void on_entry(Event const&, Fsm&)
                {
                    LOG_VERBOSE() << "[Sender] TxConfirming state";
                }
            };
            struct TxOutputConfirming : public msmf::state<>
            {
                template <class Event, class Fsm>
                void on_entry(Event const&, Fsm&)
                {
                    LOG_VERBOSE() << "[Sender] TxOutputConfirming state";
                }
            };

            FSMDefinition(sender::IGateway& gateway
                        , beam::IKeyChain::Ptr keychain
                        , TxDescription& txDesc)
                : FSMDefinitionBase{txDesc}
                , m_gateway{ gateway }
                , m_keychain{ keychain }
            {
                assert(keychain);
                update_tx_description(TxDescription::Pending);
            }

            // transition actions
            void init_tx(const msmf::none&);
            bool is_valid_signature(const TxInitCompleted& );
            bool is_invalid_signature(const TxInitCompleted& );
            void confirm_tx(const TxInitCompleted& );
            void rollback_tx(const TxFailed& );
            void cancel_tx(const TxInitCompleted& );
            void complete_tx(const TxConfirmationCompleted&);
            void complete_tx();
            void rollback_tx();

            Amount get_total() const;

            void update_tx_description(TxDescription::Status s);

            using do_serialize = int;
            using initial_state = Init;
            using d = FSMDefinition;
            struct transition_table : mpl::vector<
                //   Start                 Event                     Next                  Action                     Guard
                a_row< Init              , msmf::none              , TxInitiating        , &d::init_tx                                        >,
                a_row< Init              , TxFailed                , Terminate           , &d::rollback_tx                                    >,
                a_row< TxInitiating      , TxFailed                , Terminate           , &d::rollback_tx                                    >,
                row  < TxInitiating      , TxInitCompleted         , TxConfirming        , &d::confirm_tx           , &d::is_valid_signature  >,
                row  < TxInitiating      , TxInitCompleted         , Terminate           , &d::cancel_tx            , &d::is_invalid_signature>,
                a_row< TxConfirming      , TxConfirmationCompleted , Terminate           , &d::complete_tx                                    >,
                a_row< TxConfirming      , TxFailed                , Terminate           , &d::rollback_tx                                    >
            > {};

            template <class FSM, class Event>
            void no_transition(Event const& e, FSM&, int state)
            {
                LOG_DEBUG() << "[Sender] no transition from state " << state
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
                ar  & m_blindingExcess
                    & m_kernel;
            }

            sender::IGateway& m_gateway;
            beam::IKeyChain::Ptr m_keychain;

            ECC::Scalar::Native m_blindingExcess;
            TxKernel::Ptr m_kernel;
        };

    private:
        friend FSMHelper<Sender>;
        TxDescription m_txDesc;
        msm::back::state_machine<FSMDefinition> m_fsm;
    };
}
