#pragma once

#include "core/common.h"

namespace beam
{
    struct Wallet
    {
        struct ToNetwork
        {
            using Ptr = std::unique_ptr<ToNetwork>;

            virtual void sendTransaction(const Transaction& tx) = 0;
        };

        struct Config
        {

        };

        struct Sender
        {
            void sendInvitation();
            void sendConfirmation();
        };

        struct Receiver
        {
            void handleInvitation();
            void handleConfirmation();
        };

        Wallet();

        // TODO: remove this, just for test
        void sendDummyTransaction();

    private:
        ToNetwork::Ptr m_net;
    };
}
