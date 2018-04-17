#pragma once

#include <boost/msm/back/state_machine.hpp>
#include <boost/msm/front/state_machine_def.hpp>
#include <boost/msm/front/functor_row.hpp>
#include <iostream>
#include "wallet/common.h"

namespace beam::wallet
{
    namespace msm = boost::msm;
    namespace msmf = boost::msm::front;
    namespace mpl = boost::mpl;

    namespace sender
    {
        struct InvitationData
        {
            Uuid m_txId;
            ECC::Amount m_amount; ///??
            ECC::Hash::Value m_message;
            ECC::Point::Native m_publicSenderBlindingExcess;
            ECC::Point::Native m_publicSenderNonce;
            std::vector<Input::Ptr> m_inputs;
            std::vector<Output::Ptr> m_outputs;
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

    }

    class Sender
    {
    public:
        // events
        struct TxEventBase {};
        struct TxInitCompleted : TxEventBase {};
        struct TxInitFailed : TxEventBase {};
        struct TxConfirmationCompleted : TxEventBase {};
        struct TxConfirmationFailed : TxEventBase {};
        struct TxOutputConfirmCompleted : TxEventBase {};
        struct TxOutputConfirmFailed : TxEventBase {};

        Sender(sender::IGateway& gateway) 
            : m_gateway{gateway}
        {}

    //private:
        struct FSMDefinition : public msmf::state_machine_def<FSMDefinition>
        {
            // states
            struct Init : public msmf::state<> {};
            struct Terminate : public msmf::terminate_state<> {};
            struct TxInitiating : public msmf::state<> {};
            struct TxConfirming : public msmf::state<> {};
            struct TxOutputConfirming : public msmf::state<> {};

            // transition actions
            void initTx(const msmf::none&)
            {
                std::cout << "Sender::sentTxInitiation\n";
 //               m_gateway.send();
            }

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
                std::cout << "Sender::confirmTx\n";
  //              m_gateway.send();
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
                std::cout << "Sender::confirmChangeOutput\n";
     //           m_gateway.send();
            }

            void completeTx(const TxOutputConfirmCompleted& event)
            {
                std::cout << "Sender::completeTx\n";
     //           m_gateway.send();
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
                std::cout << "no transition from state \n";
            }

        };
        using FSM = boost::msm::back::state_machine<FSMDefinition>;

        FSM m_fsm;
    private: 
        sender::IGateway& m_gateway;
    };
}