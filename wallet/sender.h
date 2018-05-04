#pragma once

#include "wallet/common.h"
#include "wallet/keychain.h"


#include <iostream>
#include <boost/msm/front/functor_row.hpp>
#include <boost/optional.hpp>

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
            struct Init : public msmf::state<> {
                template <class Event, class Fsm>
                void on_entry(Event const&, Fsm&)
                { std::cout << "[Sender] Init state\n"; } };
            struct Terminate : public msmf::terminate_state<>
            {
                template <class Event, class Fsm>
                void on_entry(Event const&, Fsm& fsm)
                {
                    std::cout << "[Sender] Terminate state\n";
                    fsm.m_gateway.remove_sender(fsm.m_txId);
                } 
            };
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

            FSMDefinition(sender::IGateway& gateway, beam::IKeyChain::Ptr keychain, const Uuid& txId, ECC::Amount amount, Height currentHeight)
                : m_gateway{ gateway }
                , m_keychain{ keychain }
                , m_txId{ txId }
                , m_amount{ amount }
                , m_height{ currentHeight }
            {}

            // transition actions
            void initTx(const msmf::none&);

            bool isValidSignature(const TxInitCompleted& );

            bool isInvalidSignature(const TxInitCompleted& );

            void confirmTx(const TxInitCompleted& );

            void rollbackTx(const TxFailed& );

            void cancelTx(const TxInitCompleted& );

            void confirmChangeOutput(const TxConfirmationCompleted&);

            void completeTx(const TxOutputConfirmCompleted&);

            using initial_state = Init;
            using d = FSMDefinition;
            struct transition_table : mpl::vector<
                //   Start                 Event                     Next                  Action                    Guard
                a_row< Init              , msmf::none              , TxInitiating        , &d::initTx                                      >,
                a_row< TxInitiating      , TxFailed                , Terminate           , &d::rollbackTx                                  >,
                row  < TxInitiating      , TxInitCompleted         , TxConfirming        , &d::confirmTx           , &d::isValidSignature  >,
                row  < TxInitiating      , TxInitCompleted         , Terminate           , &d::cancelTx            , &d::isInvalidSignature>,
                a_row< TxConfirming      , TxConfirmationCompleted , TxOutputConfirming  , &d::confirmChangeOutput                         >,
                a_row< TxConfirming      , TxFailed                , Terminate           , &d::rollbackTx                                  >,
                a_row< TxOutputConfirming, TxOutputConfirmCompleted, Terminate           , &d::completeTx                                  >,
                a_row< TxOutputConfirming, TxFailed                , Terminate           , &d::rollbackTx                                  >
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