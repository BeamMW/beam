#pragma once

#include "wallet/common.h"
#include <iostream>
#include <boost/msm/front/functor_row.hpp>

namespace beam::wallet
{
    class Receiver : public FSMHelper<Receiver>
    {
    public:
        // interface to communicate with sender
        struct ConfirmationData
        {
            Uuid m_txId;
            ECC::Point::Native m_publicReceiverBlindingExcess;
            ECC::Point::Native m_publicReceiverNonce;
            ECC::Scalar::Native m_receiverSignature;
        };

        struct IGateway
        {
            virtual void sendTxConfirmation(const ConfirmationData&) = 0;
            virtual void registerTx(const Transaction&) = 0;
        };

        // events
        struct TxEventBase {};
        struct TxConfirmationCompleted : TxEventBase {};
        struct TxConfirmationFailed : TxEventBase {};
        struct TxRegistrationCompleted : TxEventBase {};
        struct TxRegistrationFailed : TxEventBase {};
        struct TxOutputConfirmCompleted : TxEventBase {};
        struct TxOutputConfirmFailed : TxEventBase {};
        
        Receiver(IGateway& gateway, const Uuid& txId)
            : m_fsm{boost::ref(gateway), boost::ref(txId)}
        {
        }  
    private:
        struct FSMDefinition : public msmf::state_machine_def<FSMDefinition>
        {
            // states
            struct Init : public msmf::state<> {
                template <class Event, class Fsm>
                void on_entry(Event const&, Fsm&)
                {
                    std::cout << "[Receiver] Init state\n";
                }
            };
            struct Terminate : public msmf::terminate_state<> {
                template <class Event, class Fsm>
                void on_entry(Event const&, Fsm&)
                {
                    std::cout << "[Receiver] Terminate state\n";
                }
            };
            struct TxConfirming : public msmf::state<> {
                template <class Event, class Fsm>
                void on_entry(Event const&, Fsm&)
                {
                    std::cout << "[Receiver] TxConfirming state\n";
                }
            };
            struct TxRegistering : public msmf::state<> {
                template <class Event, class Fsm>
                void on_entry(Event const&, Fsm&)
                {
                    std::cout << "[Receiver] TxRegistering state\n";
                }
            };
            struct TxOutputConfirming : public msmf::state<> {
                template <class Event, class Fsm>
                void on_entry(Event const&, Fsm&)
                {
                    std::cout << "[Receiver] TxOutputConfirming state\n";
                }
            };

            FSMDefinition(IGateway& gateway, const Uuid& txId)
                : m_gateway{ gateway }
                , m_txId{ txId }
            {}

            // transition actions
            void confirmTx(const msmf::none&)
            {
                m_confirmationData.m_txId = m_txId;
                m_gateway.sendTxConfirmation(m_confirmationData);
            }

            bool isValidSignature(const TxConfirmationCompleted& event)
            {
                std::cout << "Receiver::isValidSignature\n";
                return true;
            }

            bool isInvalidSignature(const TxConfirmationCompleted& event)
            {
                std::cout << "Receiver::isInvalidSignature\n";
                return false;
            }

            void registerTx(const TxConfirmationCompleted& event)
            {
                m_gateway.registerTx(Transaction());
            }

            void rollbackTx(const TxConfirmationFailed& event)
            {
                std::cout << "Receiver::rollbackTx\n";
            }

            void rollbackTx(const TxRegistrationFailed& event)
            {
                std::cout << "Receiver::rollbackTx\n";
            }

            void rollbackTx(const TxOutputConfirmFailed& event)
            {
                std::cout << "Receiver::rollbackTx\n";
            }

            void cancelTx(const TxConfirmationCompleted& event)
            {
                std::cout << "Receiver::cancelTx\n";
            }

            void confirmOutput(const TxRegistrationCompleted& event)
            {
                std::cout << "Receiver::confirmOutput\n";
            }

            void completeTx(const TxOutputConfirmCompleted& event)
            {
                std::cout << "Receiver::completeTx\n";
            }

            using initial_state = Init;
            using d = FSMDefinition;
            struct transition_table : mpl::vector<
                //   Start                 Event                     Next                   Action              Guard
                a_row< Init              , msmf::none              , TxConfirming         , &d::confirmTx                             >,
                a_row< TxConfirming      , TxConfirmationFailed    , Terminate            , &d::rollbackTx                            >,
                row  < TxConfirming      , TxConfirmationCompleted , TxRegistering        , &d::registerTx    , &d::isValidSignature  >,
                row  < TxConfirming      , TxConfirmationCompleted , Terminate            , &d::cancelTx      , &d::isInvalidSignature>,
                a_row< TxRegistering     , TxRegistrationCompleted , TxOutputConfirming   , &d::confirmOutput                         >,
                a_row< TxRegistering     , TxRegistrationFailed    , Terminate            , &d::rollbackTx                            >,
                a_row< TxOutputConfirming, TxOutputConfirmCompleted, Terminate            , &d::completeTx                            >,
                a_row< TxOutputConfirming, TxOutputConfirmFailed   , Terminate            , &d::rollbackTx                            >
            > {};

            template <class FSM, class Event>
            void no_transition(Event const& e, FSM&, int state)
            {
                std::cout << "Receiver: no transition from state " << state
                    << " on event " << typeid(e).name() << std::endl;
            }

            IGateway& m_gateway;
            Uuid m_txId;
            ConfirmationData m_confirmationData;
        //    Transaction m_transaction;
        };
    private:
        friend FSMHelper<Receiver>;
        msm::back::state_machine<FSMDefinition> m_fsm;

    };
}