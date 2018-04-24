#pragma once

#include "wallet/common.h"
#include "wallet/keychain.h"


#include <iostream>
#include <boost/msm/front/functor_row.hpp>

namespace beam::wallet
{
    class Sender : public FSMHelper<Sender>
    {
    public:
        // events
        struct TxEventBase {};
        struct TxInitCompleted
        {
            receiver::ConfirmationData::Ptr data;
        };
        struct TxInitFailed : TxEventBase {};
        struct TxConfirmationCompleted : TxEventBase {};
        struct TxConfirmationFailed : TxEventBase {};
        struct TxOutputConfirmCompleted : TxEventBase {};
        struct TxOutputConfirmFailed : TxEventBase {};

        Sender(sender::IGateway& gateway, const Uuid& txId, beam::IKeyChain::Ptr keychain, const ECC::Amount& amount)
            : m_fsm{boost::ref(gateway)
            , boost::ref(txId)
            , keychain
            , boost::ref(amount)}
        {
            
        }    
    private:
        struct FSMDefinition : public msmf::state_machine_def<FSMDefinition>
        {
            // states
            struct Init : public msmf::state<> {
                template <class Event, class Fsm>
                void on_entry(Event const&, Fsm&)
                { std::cout << "[Sender] Init state\n"; } };
            struct Terminate : public msmf::terminate_state<> {
                template <class Event, class Fsm>
                void on_entry(Event const&, Fsm&)
                { std::cout << "[Sender] Terminate state\n"; } };
            struct TxInitiating : public msmf::state<> {
                template <class Event, class Fsm>
                void on_entry(Event const&, Fsm&)
                { std::cout << "[Sender] TxInitiating state\n"; } };
            struct TxConfirming : public msmf::state<> {
                template <class Event, class Fsm>
                void on_entry(Event const&, Fsm&)
                { std::cout << "[Sender] TxConfirming state\n"; } };
            struct TxOutputConfirming : public msmf::state<> {
                template <class Event, class Fsm>
                void on_entry(Event const&, Fsm&)
                { std::cout << "[Sender] TxOutputConfirming state\n"; } };

            FSMDefinition(sender::IGateway& gateway, const Uuid& txId, beam::IKeyChain::Ptr keychain, const ECC::Amount& amount)
                : m_gateway{ gateway }
                , m_txId{txId}
                , m_keychain(keychain)
                , m_amount(amount)
            {}

            // transition actions
            void initTx(const msmf::none&);

            bool isValidSignature(const TxInitCompleted& event);

            bool isInvalidSignature(const TxInitCompleted& event);

            void confirmTx(const TxInitCompleted& event);

            void rollbackTx(const TxInitFailed& event)
            {
                std::cout << "Sender::rollbackTx\n";
            }

            void rollbackTx(const TxConfirmationFailed& event)
            {
                std::cout << "Sender::rollbackTx\n";
            }
            
            void rollbackTx(const TxOutputConfirmFailed& event)
            {
                std::cout << "Sender::rollbackTx\n";
            }

            void cancelTx(const TxInitCompleted& event)
            {
                std::cout << "Sender::cancelTx\n";
            }

            void confirmChangeOutput(const TxConfirmationCompleted& event)
            {
                m_gateway.sendChangeOutputConfirmation();
            }

            void completeTx(const TxOutputConfirmCompleted& event)
            {
                std::cout << "Sender::completeTx\n";
            }

            using initial_state = Init;
            using d = FSMDefinition;
            struct transition_table : mpl::vector<
                //   Start                 Event                     Next                  Action                    Guard
                a_row< Init              , msmf::none              , TxInitiating        , &d::initTx                                      >,
                a_row< TxInitiating      , TxInitFailed            , Terminate           , &d::rollbackTx                                  >,
                row  < TxInitiating      , TxInitCompleted         , TxConfirming        , &d::confirmTx           , &d::isValidSignature  >,
                row  < TxInitiating      , TxInitCompleted         , Terminate           , &d::cancelTx            , &d::isInvalidSignature>,
                a_row< TxConfirming      , TxConfirmationCompleted , TxOutputConfirming  , &d::confirmChangeOutput                         >,
                a_row< TxConfirming      , TxConfirmationFailed    , Terminate           , &d::rollbackTx                                  >,
                a_row< TxOutputConfirming, TxOutputConfirmCompleted, Terminate           , &d::completeTx                                  >,
                a_row< TxOutputConfirming, TxOutputConfirmFailed   , Terminate           , &d::rollbackTx                                  >
            > {};

            template <class FSM, class Event>
            void no_transition(Event const& e, FSM&, int state)
            {
                std::cout << "[Sender] no transition from state " << state
                    << " on event " << typeid(e).name() << std::endl;
            }

            sender::IGateway& m_gateway;
            beam::IKeyChain::Ptr m_keychain;

            Uuid m_txId;
            ECC::Amount m_amount;
            ECC::Scalar::Native m_blindingExcess;
            ECC::Scalar::Native m_nonce;
            ECC::Scalar::Native m_senderSignature;
            ECC::Point::Native m_publicBlindingExcess;
            ECC::Point::Native m_publicNonce;
            TxKernel m_kernel;
        };
        
    protected:
        friend FSMHelper<Sender>;
        msm::back::state_machine<FSMDefinition> m_fsm;
    };
}