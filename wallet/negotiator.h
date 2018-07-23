#pragma once

#include "wallet/common.h"
#include "wallet/wallet_db.h"

#include <boost/optional.hpp>
#include "utility/logger.h"

namespace beam::wallet
{
    namespace events
    {
        struct TxRegistrationCompleted {};
        struct TxInvited {};
        struct TxInitiated {};
        struct TxConfirmationCompleted { ConfirmTransaction data; };
        struct TxInvitationCompleted { ConfirmInvitation data; };
        struct TxOutputsConfirmed {};
        struct TxFailed
        {
            bool m_notify;
            TxFailed(bool notify = false) : m_notify{ notify } {}
        };
    }

    class Negotiator
    {
    public:
        using Ptr = std::shared_ptr<Negotiator>;

        Negotiator(INegotiatorGateway& gateway
            , beam::IKeyChain::Ptr keychain
            , const TxDescription& txDesc);

		bool ProcessInvitation(Invite& inviteMsg);

        void start()
        {
            m_fsm.start();
        }

        void stop()
        {
            m_fsm.stop();
        }

        template<typename Event>
        bool process_event(const Event& event)
        {
            auto res = m_fsm.process_event(event) == msm::back::HANDLED_TRUE;
            return res;
        }

        template<class Archive>
        void serialize(Archive & ar, const unsigned int)
        {
            m_fsm.serialize(ar, 0);
        }

        // for test only
        const int* current_state() const
        {
            return m_fsm.current_state();
        }

        struct FSMDefinition : public msmf::state_machine_def<FSMDefinition>
        {
            // states
            struct TxAllOk : public msmf::state<>
            {

            };

            struct TxInitial : public msmf::state<>
            {
                template <class Event, class Fsm>
                void on_entry(Event const&, Fsm&)
                {
                    LOG_VERBOSE() << "TxInitial state";
                }
            };
            struct TxTerminal : public msmf::terminate_state<>
            {
                template <class Event, class Fsm>
                void on_entry(Event const&, Fsm& fsm)
                {
                    LOG_VERBOSE() << "TxTerminal state";
                    fsm.m_parent.m_gateway.on_tx_completed(fsm.m_parent.m_txDesc);
                }
            };
            struct TxInvitation : public msmf::state<>
            {
                template <class Event, class Fsm>
                void on_entry(Event const&, Fsm& fsm)
                {
                    LOG_VERBOSE() << "TxInvitation state";
                    fsm.sendInvite();
                }
            };
            struct TxConfirmation : public msmf::state<>
            {
                template <class Event, class Fsm>
                void on_entry(Event const&, Fsm&)
                {
                    LOG_VERBOSE() << "TxConfirmation state";
                }
            };
            struct TxRegistration : public msmf::state<>
            {
                template <class Event, class Fsm>
                void on_entry(Event const&, Fsm&)
                {
                    LOG_VERBOSE() << "TxRegistration state";
                }
            };
            struct TxOutputsConfirmation : public msmf::state<>
            {
                template <class Event, class Fsm>
                void on_entry(Event const&, Fsm&)
                {
                    LOG_VERBOSE() << "TxOutputsConfirmation state";
                }
            };

            FSMDefinition(Negotiator& parent);

            // transition actions
            void confirmInvitation(const events::TxInvited&);
            void invitePeer(const events::TxInitiated&);
            void registerTx(const events::TxConfirmationCompleted&);
            void confirmPeer(const events::TxInvitationCompleted&);
            void completeTx(const events::TxRegistrationCompleted&);
            void confirmOutputs(const events::TxRegistrationCompleted&);
            void completeTx(const events::TxOutputsConfirmed&);

            void rollbackTx(const events::TxFailed& );
            void completeTx();
            void rollbackTx();

            void sendInvite() const;

            Amount get_total() const;

            void update_tx_description(TxDescription::Status s);

            bool getSenderInputsAndOutputs(const Height& currentHeight, std::vector<Input::Ptr>& inputs, std::vector<Output::Ptr>& outputs);
            bool prepareSenderUtxos(const Height& currentHeight);

			bool registerTxInternal(const events::TxConfirmationCompleted&);
			bool confirmPeerInternal(const events::TxInvitationCompleted&);

            using do_serialize = int;
            typedef int no_message_queue;
            typedef msm::active_state_switch_after_transition_action active_state_switch_policy;


            using initial_state = mpl::vector<TxInitial, TxAllOk>;
            using d = FSMDefinition;

            struct transition_table : mpl::vector<
                //   Start                      Event                             Next                  Action                     
                a_row< TxInitial                , events::TxInvited              , TxConfirmation      , &d::confirmInvitation    >,
                a_row< TxInitial                , events::TxInitiated            , TxInvitation        , &d::invitePeer           >,
                
                a_row< TxConfirmation           , events::TxConfirmationCompleted, TxRegistration      , &d::registerTx           >,
                a_row< TxInvitation             , events::TxInvitationCompleted  , TxRegistration      , &d::confirmPeer          >,

                //a_row< TxRegistration         , events::TxRegistrationCompleted , TxOutputsConfirmation , &d::confirmOutputs             >,
                a_row< TxRegistration           , events::TxRegistrationCompleted, TxTerminal          , &d::completeTx           >,

                //a_row< TxOutputsConfirmation  , events::TxOutputsConfirmed      , TxTerminal            , &d::completeTx                 >,

                a_row< TxAllOk                , events::TxFailed                , TxTerminal            , &d::rollbackTx                 >
            > {};


            template <class FSM, class Event>
            void no_transition(Event const& e, FSM& , int state)
            {
                LOG_DEBUG() << "[Sender] no transition from state " << state
                            << " on event " << typeid(e).name();
            }

            template <class FSM, class Event>
            void exception_caught(Event const&, FSM& fsm, std::exception& ex)
            {
                LOG_ERROR() << ex.what();
                fsm.process_event(events::TxFailed());
            }

            template<typename Archive>
            void serialize(Archive & ar, const unsigned int)
            {
                ar & m_blindingExcess
                   & m_offset
                   & m_peerSignature
                   & m_publicPeerExcess
                   & m_publicPeerNonce
                   & m_transaction
                   & m_kernel;
            }

            void createKernel(Amount fee, Height minHeight);
            Input::Ptr createInput(const Coin& utxo);
            Output::Ptr createOutput(Amount amount, Height height);
            void createOutputUtxo(Amount amount, Height height);
            ECC::Scalar createSignature();
            void createSignature2(ECC::Scalar& partialSignature, ECC::Point& publicNonce);
            ECC::Point getPublicExcess() const;
            ECC::Point getPublicNonce() const;
            bool isValidSignature(const ECC::Scalar& peerSignature) const;
            bool isValidSignature(const ECC::Scalar& peerSignature, const ECC::Point& publicPeerNonce, const ECC::Point& publicPeerExcess) const;
            std::vector<Input::Ptr> getTxInputs(const TxID& txID) const;
            std::vector<Output::Ptr> getTxOutputs(const TxID& txID) const;

            Negotiator& m_parent;

            ECC::Scalar::Native m_blindingExcess;
            ECC::Scalar::Native m_offset;
            ECC::Scalar::Native m_peerSignature;
            ECC::Point::Native m_publicPeerExcess;
            ECC::Point::Native m_publicPeerNonce;
            Transaction::Ptr m_transaction;
            TxKernel::Ptr m_kernel;
        };

    private:
        using Fsm = msm::back::state_machine<FSMDefinition>;
        friend Fsm;
        
        INegotiatorGateway& m_gateway;
        beam::IKeyChain::Ptr m_keychain;

        TxDescription m_txDesc;

        Fsm m_fsm;
    };
}
