#pragma once

#include "wallet/common.h"
#include "wallet/keychain.h"


#include <iostream>
#include <boost/msm/front/functor_row.hpp>
#include <boost/optional.hpp>
#include "utility/logger.h"

namespace beam::wallet
{
    class Sender : public FSMHelper<Sender>
    {
    public:
        using Ptr = std::unique_ptr<Sender>;
        // events
        struct TxFailed {};
        struct TxInitCompleted
        {
            receiver::ConfirmationData::Ptr data;
        };
        struct TxConfirmationCompleted {};
        struct TxOutputConfirmCompleted {};

        Sender(sender::IGateway& gateway, beam::IKeyChain::Ptr keychain, const Uuid& txId, const ECC::Amount& amount, const Height& currentHeight)
            : m_fsm{boost::ref(gateway), keychain, boost::ref(txId), boost::ref(amount), boost::ref(currentHeight)}
        {
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
                    LOG_DEBUG() << "[Sender] Init state";
                } 
            };
            struct Terminate : public msmf::terminate_state<>
            {
                template <class Event, class Fsm>
                void on_entry(Event const&, Fsm& fsm)
                {
                    LOG_DEBUG() << "[Sender] Terminate state";
                    fsm.m_gateway.on_tx_completed(fsm.m_txId);
                } 
            };
            struct TxInitiating : public msmf::state<>
            {
                template <class Event, class Fsm>
                void on_entry(Event const&, Fsm&)
                {
                    LOG_DEBUG() << "[Sender] TxInitiating state"; 
                } 
            };
            struct TxConfirming : public msmf::state<>
            {
                template <class Event, class Fsm>
                void on_entry(Event const&, Fsm&)
                {
                    LOG_DEBUG() << "[Sender] TxConfirming state"; 
                } 
            };
            struct TxOutputConfirming : public msmf::state<>
            {
                template <class Event, class Fsm>
                void on_entry(Event const&, Fsm&)
                { 
                    LOG_DEBUG() << "[Sender] TxOutputConfirming state"; 
                }
            };

            FSMDefinition(sender::IGateway& gateway, beam::IKeyChain::Ptr keychain, const Uuid& txId, ECC::Amount amount, Height currentHeight)
                : m_gateway{ gateway }
                , m_keychain{ keychain }
                , m_txId{ txId }
                , m_amount{ amount }
                , m_height{ currentHeight }
            {}

            // transition actions
            void init_tx(const msmf::none&);

            bool is_valid_signature(const TxInitCompleted& );

            bool is_invalid_signature(const TxInitCompleted& );

            void confirm_tx(const TxInitCompleted& );

            void rollback_tx(const TxFailed& );

            void cancel_tx(const TxInitCompleted& );

            void confirm_change_output(const TxConfirmationCompleted&);

            void complete_tx(const TxOutputConfirmCompleted&);

            using initial_state = Init;
            using d = FSMDefinition;
            struct transition_table : mpl::vector<
                //   Start                 Event                     Next                  Action                     Guard
                a_row< Init              , msmf::none              , TxInitiating        , &d::init_tx                                        >,
                a_row< Init              , TxFailed                , Terminate           , &d::rollback_tx                                    >,
                a_row< TxInitiating      , TxFailed                , Terminate           , &d::rollback_tx                                    >,
                row  < TxInitiating      , TxInitCompleted         , TxConfirming        , &d::confirm_tx           , &d::is_valid_signature  >,
                row  < TxInitiating      , TxInitCompleted         , Terminate           , &d::cancel_tx            , &d::is_invalid_signature>,
                a_row< TxConfirming      , TxConfirmationCompleted , TxOutputConfirming  , &d::confirm_change_output                          >,
                a_row< TxConfirming      , TxFailed                , Terminate           , &d::rollback_tx                                    >,
                a_row< TxOutputConfirming, TxOutputConfirmCompleted, Terminate           , &d::complete_tx                                    >,
                a_row< TxOutputConfirming, TxFailed                , Terminate           , &d::rollback_tx                                    >
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

            sender::IGateway& m_gateway;
            beam::IKeyChain::Ptr m_keychain;

            Uuid m_txId;
            ECC::Amount m_amount;
            Height m_height;
            ECC::Scalar::Native m_blindingExcess;
            ECC::Scalar::Native m_nonce;
            ECC::Scalar::Native m_senderSignature;
            ECC::Point::Native m_publicBlindingExcess;
            ECC::Point::Native m_publicNonce;
            TxKernel m_kernel;

            std::vector<Coin> m_coins;
            boost::optional<Coin> m_changeOutput;
        };
        
    protected:
        friend FSMHelper<Sender>;
        msm::back::state_machine<FSMDefinition> m_fsm;
    };
}