#pragma once

#include <boost/msm/back/state_machine.hpp>
#include <boost/msm/front/state_machine_def.hpp>
#include <boost/msm/front/functor_row.hpp>
#include "wallet/common.h"
#include <iostream>

namespace beam::wallet
{
    namespace msm = boost::msm;
    namespace msmf = boost::msm::front;
    namespace mpl = boost::mpl;

    class Receiver
    {
    public:
        // interface to communicate with 
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

        Receiver(IGateway& gateway)
            : m_fsm{boost::ref(gateway)}
        {
            m_fsm.start();
        }

        template<typename Event>
        bool processEvent(const Event& event)
        {
            return m_fsm.process_event(event) == msm::back::HANDLED_TRUE;
        }

    private:
        struct FSMDefinition : public msmf::state_machine_def<FSMDefinition>
        {
            // states
            struct Init : public msmf::state<> {};
            struct Terminate : public msmf::terminate_state<> {};
            struct TxConfirming : public msmf::state<> {};
            struct TxRegistering : public msmf::state<> {};
            struct TxOutputConfirming : public msmf::state<> {};

            FSMDefinition(IGateway& gateway)
                : m_gateway{ gateway }
            {}

            // transition actions
            void confirmTx(const msmf::none&)
            {
                m_gateway.sendTxConfirmation(ConfirmationData());
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
                std::cout << "no transition from state \n";
            }

            IGateway& m_gateway;
        };
        msm::back::state_machine<FSMDefinition> m_fsm;
    };
}