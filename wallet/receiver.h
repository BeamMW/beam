#pragma once

#include "wallet/common.h"
#include "wallet/keychain.h"
#include "wallet/sender.h"

#include <iostream>
#include <boost/msm/front/functor_row.hpp>

namespace beam::wallet
{
    class Receiver : public FSMHelper<Receiver>
    {
    public:
        using Ptr = std::unique_ptr<Receiver>;
        // events
        struct TxFailed {};
        struct TxConfirmationCompleted
        {
            sender::ConfirmationData::Ptr data;
        };
        struct TxRegistrationCompleted 
        {
            Uuid m_txId;
        };
        struct TxOutputConfirmCompleted {};
        
        Receiver(receiver::IGateway& gateway, beam::IKeyChain::Ptr keychain, sender::InvitationData::Ptr initData)
            : m_fsm{boost::ref(gateway), keychain, initData}
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
                void on_entry(Event const&, Fsm& fsm)
                {
                    std::cout << "[Receiver] Terminate state\n";
                    fsm.m_gateway.removeReceiver(fsm.m_txId);
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

            FSMDefinition(receiver::IGateway &gateway, beam::IKeyChain::Ptr keychain, sender::InvitationData::Ptr initData);

            // transition actions
            void confirmTx(const msmf::none&);

            bool isValidSignature(const TxConfirmationCompleted& event);

            bool isInvalidSignature(const TxConfirmationCompleted& event);

            void registerTx(const TxConfirmationCompleted& event);

            void rollbackTx(const TxFailed& event);

            void cancelTx(const TxConfirmationCompleted& event);

            void confirmOutput(const TxRegistrationCompleted& event);

            void completeTx(const TxOutputConfirmCompleted& event);

            using initial_state = Init;
            using d = FSMDefinition;
            struct transition_table : mpl::vector<
                //   Start                 Event                     Next                   Action              Guard
                a_row< Init              , msmf::none              , TxConfirming         , &d::confirmTx                             >,
                a_row< TxConfirming      , TxFailed                , Terminate            , &d::rollbackTx                            >,
                row  < TxConfirming      , TxConfirmationCompleted , TxRegistering        , &d::registerTx    , &d::isValidSignature  >,
                row  < TxConfirming      , TxConfirmationCompleted , Terminate            , &d::cancelTx      , &d::isInvalidSignature>,
                a_row< TxRegistering     , TxRegistrationCompleted , TxOutputConfirming   , &d::confirmOutput                         >,
                a_row< TxRegistering     , TxFailed                , Terminate            , &d::rollbackTx                            >,
                a_row< TxOutputConfirming, TxOutputConfirmCompleted, Terminate            , &d::completeTx                            >,
                a_row< TxOutputConfirming, TxFailed                , Terminate            , &d::rollbackTx                            >
            > {};

            template <class FSM, class Event>
            void no_transition(Event const& e, FSM&, int state)
            {
                std::cout << "Receiver: no transition from state " << state
                    << " on event " << typeid(e).name() << std::endl;
            }

            receiver::IGateway& m_gateway;
            beam::IKeyChain::Ptr m_keychain;

            Uuid m_txId;

            ECC::Amount m_amount; 
            ECC::Hash::Value m_message;
            std::vector<Input::Ptr> m_inputs;
            std::vector<Output::Ptr> m_outputs;
            
            ECC::Point::Native m_publicReceiverBlindingExcess;
            ECC::Point::Native m_publicSenderBlindingExcess;
            ECC::Point::Native m_publicSenderNonce;
            ECC::Scalar::Native m_receiverSignature;
            ECC::Scalar::Native m_blindingExcess;
            ECC::Scalar::Native m_nonce;
            ECC::Scalar::Native m_schnorrChallenge;

            TransactionPtr m_transaction;
            TxKernel* m_kernel;
        };

        friend FSMHelper<Receiver>;
        msm::back::state_machine<FSMDefinition> m_fsm;

    };
}