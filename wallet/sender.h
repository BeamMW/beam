#pragma once

#include "wallet/common.h"
#include "wallet/wallet_db.h"

#include <boost/optional.hpp>
#include "utility/logger.h"

namespace beam::wallet
{
    namespace events
    {
        struct TxRegistrationCompleted
        {
            Uuid m_txId;
        };
        struct TxConfirmationCompleted2 { ConfirmTransaction data; };
        struct TxSenderInvited {};
        struct TxReceiverInvited { InviteReceiver data; };
        struct TxSend {};
        struct TxBill {};
        struct TxConfirmationCompleted { ConfirmTransaction data; };
        struct TxInvitationCompleted { ConfirmInvitation data; };
        struct TxOutputsConfirmed {};
        struct TxFailed {};
    }
    

    class Sender : public FSMHelper<Sender>
    {
    public:
        using Ptr = std::shared_ptr<Sender>;

        Sender(sender::IGateway& gateway
             , beam::IKeyChain::Ptr keychain
             , const TxDescription& txDesc )
            : m_gateway{gateway}
            , m_keychain{keychain}
            , m_txDesc{txDesc}
            , m_fsm{std::ref(*this)}
        {
            assert(keychain);
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
            struct TxReceiverInvitation : public msmf::state<>
            {
                template <class Event, class Fsm>
                void on_entry(Event const&, Fsm&)
                {
                    LOG_VERBOSE() << "TxReceiverInvitation state";
                }
            };
            struct TxSenderInvitation : public msmf::state<>
            {
                template <class Event, class Fsm>
                void on_entry(Event const&, Fsm&)
                {
                    LOG_VERBOSE() << "TxSenderInvitation state";
                }
            };
            struct TxBillConfirmation : public msmf::state<>
            {
                template <class Event, class Fsm>
                void on_entry(Event const&, Fsm&)
                {
                    LOG_VERBOSE() << "TxBillConfirmation state";
                }
            };
            struct TxSendingConfirmation : public msmf::state<>
            {
                template <class Event, class Fsm>
                void on_entry(Event const&, Fsm&)
                {
                    LOG_VERBOSE() << "TxSendingConfirmation state";
                }
            };
            struct TxSenderConfirmation : public msmf::state<>
            {
                template <class Event, class Fsm>
                void on_entry(Event const&, Fsm&)
                {
                    LOG_VERBOSE() << "TxSenderConfirmation state";
                }
            };
            struct TxReceiverConfirmation : public msmf::state<>
            {
                template <class Event, class Fsm>
                void on_entry(Event const&, Fsm&)
                {
                    LOG_VERBOSE() << "TxReceiverConfirmation state";
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

            FSMDefinition(Sender& parent)
                : m_parent{parent}
            {
                update_tx_description(TxDescription::Pending);
            }

            // transition actions
            bool isValidSignature(const events::TxInvitationCompleted&);
            bool isValidSignature(const events::TxConfirmationCompleted&);
            void rollbackTx(const events::TxFailed& );
            void complete_tx();
            void rollbackTx();

            void confirmSenderInvitation(const events::TxSenderInvited&);
            void confirmReceiverInvitation(const events::TxReceiverInvited&);
            void inviteReceiver(const events::TxSend&);
            void inviteSender(const events::TxBill&);
            void registerTx(const events::TxConfirmationCompleted&);
            void confirmReceiver(const events::TxInvitationCompleted&);
            void confirmSender(const events::TxInvitationCompleted&);
            void confirmOutputs(const events::TxRegistrationCompleted&);
            void confirmOutputs(const events::TxConfirmationCompleted&);
            void completeTx(const events::TxOutputsConfirmed&);

            Amount get_total() const;

            void update_tx_description(TxDescription::Status s);

            using do_serialize = int;
            typedef int no_message_queue;

            using initial_state = mpl::vector<TxInitial, TxAllOk>;
            using d = FSMDefinition;

            struct transition_table : mpl::vector<
                //   Start                      Event                             Next                  Action                     Guard
                a_row< TxInitial              , events::TxSenderInvited         , TxBillConfirmation    , &d::confirmSenderInvitation    >,
                a_row< TxInitial              , events::TxReceiverInvited       , TxSendingConfirmation , &d::confirmReceiverInvitation  >,
                a_row< TxInitial              , events::TxSend                  , TxReceiverInvitation  , &d::inviteReceiver             >,
                a_row< TxInitial              , events::TxBill                  , TxSenderInvitation    , &d::inviteSender               >,

                a_row< TxBillConfirmation     , events::TxConfirmationCompleted , TxRegistration        , &d::registerTx                 >,
                a_row< TxSendingConfirmation  , events::TxConfirmationCompleted , TxRegistration        , &d::registerTx                 >,
                a_row< TxReceiverInvitation   , events::TxInvitationCompleted   , TxReceiverConfirmation, &d::confirmReceiver            >,
                a_row< TxSenderInvitation     , events::TxInvitationCompleted   , TxSenderConfirmation  , &d::confirmSender              >,

                a_row< TxRegistration         , events::TxRegistrationCompleted , TxOutputsConfirmation , &d::confirmOutputs             >,
                a_row< TxReceiverConfirmation , events::TxConfirmationCompleted , TxOutputsConfirmation , &d::confirmOutputs             >,
                a_row< TxSenderConfirmation   , events::TxConfirmationCompleted , TxOutputsConfirmation , &d::confirmOutputs             >,

                a_row< TxOutputsConfirmation  , events::TxOutputsConfirmed      , TxTerminal            , &d::completeTx                 >,
                a_row< TxAllOk                , events::TxFailed                , TxTerminal            , &d::rollbackTx                 >
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
                fsm.process_event(events::TxFailed());
            }

            template<typename Archive>
            void serialize(Archive & ar, const unsigned int)
            {
       /*         ar  & m_blindingExcess
                    & m_kernel;*/
            }
            Sender& m_parent;
        };

    private:
        friend FSMHelper<Sender>;
        friend msm::back::state_machine<FSMDefinition>;

        sender::IGateway& m_gateway;
        beam::IKeyChain::Ptr m_keychain;

        TxDescription m_txDesc;

        ECC::Scalar::Native m_blindingExcess;
        ECC::Point::Native m_publicPeerBlindingExcess;
        ECC::Point::Native m_publicPeerNonce;
        ECC::Hash::Value m_message;
        Transaction::Ptr m_transaction;
        TxKernel::Ptr m_kernel;

        msm::back::state_machine<FSMDefinition> m_fsm;
    };
}
