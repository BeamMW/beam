#pragma once

#include "wallet/common.h"
#include "wallet/keychain.h"

#include <boost/msm/back/state_machine.hpp>
#include <boost/msm/front/state_machine_def.hpp>
#include <boost/msm/front/functor_row.hpp>
#include <iostream>

namespace beam::wallet
{
    namespace msm = boost::msm;
    namespace msmf = boost::msm::front;
    namespace mpl = boost::mpl;

    class Sender
    {
    public:
        // interface to communicate with receiver
        struct InvitationData
        {
            Uuid m_txId;
            ECC::Amount m_amount; ///??
            ECC::Hash::Value m_message;
            ECC::Point::Native m_publicSenderBlindingExcess;
            ECC::Point::Native m_publicSenderNonce;
            //std::vector<Input::Ptr> m_inputs;
            //std::vector<Output::Ptr> m_outputs;
        };

        struct ConfirmationData
        {
            Uuid m_txId;
            ECC::Scalar::Native m_senderSignature;
        };

        struct IGateway
        {
            virtual void sendTxInitiation(const InvitationData&) = 0;
            virtual void sendTxConfirmation(const ConfirmationData&) = 0;
            virtual void sendChangeOutputConfirmation() = 0;
        };

        // events
        struct TxEventBase {};
        struct TxInitCompleted : TxEventBase {};
        struct TxInitFailed : TxEventBase {};
        struct TxConfirmationCompleted : TxEventBase {};
        struct TxConfirmationFailed : TxEventBase {};
        struct TxOutputConfirmCompleted : TxEventBase {};
        struct TxOutputConfirmFailed : TxEventBase {};

        Sender(IGateway& gateway, const Uuid& txId, beam::IKeyChain::Ptr keychain, const ECC::Amount& amount)
            : m_fsm{boost::ref(gateway), boost::ref(txId)}
            , m_keychain(keychain)
            , m_amount(amount)
        {
            
        }

        void start()
        {
            m_fsm.start();
        }

        template<typename Event>
        bool processEvent(const Event& event)
        {
            return m_fsm.process_event(event) == msm::back::HANDLED_TRUE;
        }

        template<typename Event>
        void enqueueEvent(const Event& event)
        {
            m_fsm.enqueue_event(event);
        }

        void executeQueuedEvents()
        {
            m_fsm.execute_queued_events();
        }

    private:
        struct FSMDefinition : public msmf::state_machine_def<FSMDefinition>
        {
            // states
            struct Init : public msmf::state<> {
                template <class Event, class Fsm>
                void on_entry(Event const&, Fsm&)
                { std::cout << "[Sender] Init stat\n"; } };
            struct Terminate : public msmf::terminate_state<> {
                template <class Event, class Fsm>
                void on_entry(Event const&, Fsm&)
                { std::cout << "[Sender] Terminate stat\n"; } };
            struct TxInitiating : public msmf::state<> {
                template <class Event, class Fsm>
                void on_entry(Event const&, Fsm&)
                { std::cout << "[Sender] TxInitiating stat\n"; } };
            struct TxConfirming : public msmf::state<> {
                template <class Event, class Fsm>
                void on_entry(Event const&, Fsm&)
                { std::cout << "[Sender] TxConfirming stat\n"; } };
            struct TxOutputConfirming : public msmf::state<> {
                template <class Event, class Fsm>
                void on_entry(Event const&, Fsm&)
                { std::cout << "[Sender] TxOutputConfirming stat\n"; } };

            FSMDefinition(IGateway& gateway, const Uuid& txId)
                : m_gateway{ gateway }
                , m_txId{txId}
            {}

            // transition actions
            void initTx(const msmf::none&);

            bool isValidSignature(const TxInitCompleted& event)
            {
                std::cout << "Sender::isValidSignature\n";
                return true;
            }

            bool isInvalidSignature(const TxInitCompleted& event)
            {
                std::cout << "Sender::isInvalidSignature\n";
                return false;
            }

            void confirmTx(const TxInitCompleted& event)
            {
                m_confirmationData.m_txId = m_txId;
                m_gateway.sendTxConfirmation(m_confirmationData);
            }

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

            IGateway& m_gateway;
            Uuid m_txId;
            InvitationData m_invitationData;
            ConfirmationData m_confirmationData;
        };
        msm::back::state_machine<FSMDefinition> m_fsm;

        beam::IKeyChain::Ptr m_keychain;
        const ECC::Amount& m_amount;
    };
}